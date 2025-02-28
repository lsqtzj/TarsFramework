﻿/**
 * Tencent is pleased to support the open source community by making Tars available.
 *
 * Copyright (C) 2016THL A29 Limited, a Tencent company. All rights reserved.
 *
 * Licensed under the BSD 3-Clause License (the "License"); you may not use this file except 
 * in compliance with the License. You may obtain a copy of the License at
 *
 * https://opensource.org/licenses/BSD-3-Clause
 *
 * Unless required by applicable law or agreed to in writing, software distributed 
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR 
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the 
 * specific language governing permissions and limitations under the License.
 */

#include "CommandLoad.h"
#include "RegistryProxy.h"

std::mutex CommandLoad::_mutex;
set<int> CommandLoad::_allPorts;

//////////////////////////////////////////////////////////////
//
ServerCommand::ExeStatus CommandLoad::canExecute(string& sResult)
{
    TC_ThreadRecLock::Lock lock(*_serverObjectPtr);

    NODE_LOG("KeepAliveThread")->debug() << "CommandLoad::canExecute " << _desc.application << "." << _desc.serverName << "|" << _desc.setId << "| beging loaded------|" << endl;

    ServerObject::InternalServerState eState = _serverObjectPtr->getInternalState();

    if (_desc.application == "" || _desc.serverName == "")
    {
        NODE_LOG("KeepAliveThread")->debug() << "CommandLoad::canExecute app or server name is empty"<< endl;
        return DIS_EXECUTABLE;
    }

    if (_serverObjectPtr->toStringState(eState).find("ing") != string::npos && eState != ServerObject::Activating)
    {
        NODE_LOG("KeepAliveThread")->debug() << "CommandLoad::canExecute cannot loading the config, the server state is "<<_serverObjectPtr->toStringState(eState)<< endl;
        return DIS_EXECUTABLE;
    }

    //设当前状态为正在loading
//    _statExChange = new StatExChange(_serverObjectPtr, ServerObject::Loading, eState);

    return EXECUTABLE;
}


//////////////////////////////////////////////////////////////
//
int CommandLoad::execute(string& sResult)
{
    //初始化服务目录信息
    _serverDir     = TC_Common::trim(_desc.basePath);
    _exePath       = TC_Common::trim(_desc.exePath);
    _serverType    = TC_Common::lower(TC_Common::trim(_desc.serverType));

    //若serverDir不合法采用默认路径
    if (_serverDir.empty() || TC_File::isAbsolute(_serverDir) ==  false)
    {
        _serverDir = TC_File::simplifyDirectory(_nodeInfo.dataDir + FILE_SEP +  _desc.application + "." + _desc.serverName);
    }

//获取服务框架配置文件
    _confPath      = _serverDir + FILE_SEP + "conf" + FILE_SEP;
    _confFile      = _confPath + _desc.application + "." + _desc.serverName + ".config.conf";

    if(_desc.application == "tars") {
        //tars服务特殊处理, 第一次用安装时的配置文件覆盖tarsnode data目录下服务的配置, 保证local端口不变化!
        string oldConf = TC_File::simplifyDirectory(_nodeInfo.dataDir + FILE_SEP + ".." + FILE_SEP + ".." + FILE_SEP + _desc.serverName + FILE_SEP + "conf" + FILE_SEP + _desc.application + "." + _desc.serverName + ".config.conf");

        NODE_LOG("KeepAliveThread")->debug() << "oldConf:" << oldConf << ", confFile:" << _confFile << endl;

        if(!TC_File::isFileExist(_confFile) && TC_File::isFileExist(oldConf)) {
            TC_File::makeDirRecursive(_confPath);
            TC_File::copyFile(oldConf, _confFile);
        }

    }

    //若exePath不合法采用默认路径
    //注意java服务启动方式特殊 可执行文件为java 须特殊处理
    if (_exePath.empty())
    {
        _exePath =  _serverDir + FILE_SEP + "bin" + FILE_SEP;
        if (_serverType == "tars_java")
        {
            try
            {
                TC_Config conf;
                conf.parseString(_desc.profile);
    
                _exeFile = conf.get("/tars/application/server<java>", "java");
            }
            catch(exception &ex)
            {
                NODE_LOG("KeepAliveThread")->error() << "parse template error:" << ex.what() << endl;

                _exeFile = "java";
            }
        }
        else if (_serverType == "tars_nodejs")
        {
            try
            {
                TC_Config conf;
                conf.parseFile(_desc.profile);
                _exeFile = conf.get("/tars/application/server<nodejs>", "node");
            }
            catch(exception &ex)
            {
                NODE_LOG("KeepAliveThread")->error() << "parse template error:" << ex.what() << endl;

                _exeFile = "node"; 
            }
        }
        else if(_serverType == "tars_php")
        {
            try
            {  
                TC_Config conf;
                conf.parseFile(_desc.profile);
                _exeFile = conf.get("/tars/application/server<php>", "php");
            }
            catch(exception &ex)
            {
                NODE_LOG("KeepAliveThread")->error() << "parse template error:" << ex.what() << endl;
                
                _exeFile = "php";
            }
        }
        else
        {
            _exeFile = _exePath + _desc.serverName; 
        }
    }
    else if (TC_File::isAbsolute(_exePath) ==  false)
    {
        //此时_desc.exePath为手工指定，手工指定时_desc.exePath为文件 所以路径要扣除可执行文件名
        //而且可执行文件名可以不等于_strServerName 用来用同一可执行文件，不同配置启动多个服务
        _exeFile =  _serverDir + FILE_SEP + "bin" + FILE_SEP + _exePath;
        _exePath = TC_File::extractFilePath(_exeFile);
    }
    else
    {
        //此时_desc.exePath为手工指定，手工指定时_desc.exePath为文件 所以路径要扣除可执行文件名
        //而且可执行文件名可以不等于_strServerName 用来用同一可执行文件，不同配置启动多个服务
        _exeFile   = _desc.exePath;
        _exePath   = _serverType == "tars_java" ? _serverDir + FILE_SEP + "bin" + FILE_SEP : TC_File::extractFilePath(_desc.exePath);
    }

    _exeFile = TC_File::simplifyDirectory(_exeFile);

    _exePath = TC_File::simplifyDirectory(_exePath) + FILE_SEP;

    //启动脚本处理
    _startScript   = TC_Common::trim(_desc.startScript);
    if (!_startScript.empty() && TC_File::isAbsolute(_startScript) ==  false)
    {
        _startScript =  _exePath + _startScript;
    }

    //停止脚本处理
    _stopScript   = TC_Common::trim(_desc.stopScript);
    if (!_stopScript.empty() && TC_File::isAbsolute(_stopScript) ==  false)
    {
        _stopScript =  _exePath + _stopScript;
    }

    //监控脚本处理
    _monitorScript   = TC_Common::trim(_desc.monitorScript);
    if (!_monitorScript.empty() && TC_File::isAbsolute(_monitorScript) ==  false)
    {
        _monitorScript =  _exePath + _monitorScript;
    }

    _startScript   = TC_File::simplifyDirectory(_startScript);
    _stopScript    = TC_File::simplifyDirectory(_stopScript);
    _monitorScript = TC_File::simplifyDirectory(_monitorScript);

    //创建配置lib文件目录
    _libPath       = _nodeInfo.dataDir + FILE_SEP + "lib" + FILE_SEP;

	NODE_LOG("KeepAliveThread")->debug() << "CommandLoad::execute"<< _serverType   << ","
                << "exe_path="      << _exePath      << "," 
                << "exe_file="      << _exeFile      << "," 
                << "start_script="  << _startScript  << "," 
                << "stop_script="   << _stopScript   << "," 
                << "monitor_script="<< _monitorScript<< "," 
                << "config_file="   << _confFile     << endl;

    //创建目录
    if (!TC_File::makeDirRecursive(_exePath))
    {
	    NODE_LOG("KeepAliveThread")->error() << "CommandLoad::execute cannot create dir: "<<(_exePath + " erro:" + strerror(errno))<<endl;
        return -1;
    }

    if (!TC_File::makeDirRecursive(_libPath))
    {
	    NODE_LOG("KeepAliveThread")->error() << "CommandLoad::execute cannot create dir: "<<(_libPath + " erro:" + strerror(errno))<<endl;
        return -1;
    }

    if (!TC_File::makeDirRecursive(_confPath))
    {
	    NODE_LOG("KeepAliveThread")->error() << "CommandLoad::execute cannot create dir: "<<(_confPath + " erro:" + strerror(errno))<<endl;
        return -1;
    }

    if (updateConfigFile(sResult) != 0)
    {
	    NODE_LOG("KeepAliveThread")->error() << "CommandLoad::execute update config error"<<endl;
        return -1;
    }

    if(_succ)
    {
        getRemoteConf();
    }

    return 0;
}

int CommandLoad::updateConfigFile(string& sResult)
{
    try
    {
        //node根据server desc生成配置。
        TC_Config           tConf;
        TC_Endpoint         tEp;
        map<string, string> m;

        m["node"] = ServerConfig::Application + "." + ServerConfig::ServerName + ".ServerObj@" + g_app.getAdapterEndpoint("ServerAdapter").toString();
        tConf.insertDomainParam("/tars/application/server", m, true);
        m.clear();

        map<string, AdapterDescriptor>::const_reverse_iterator itAdapters;
        for (itAdapters = _desc.adapters.rbegin(); itAdapters != _desc.adapters.rend(); itAdapters++)
        {
	        NODE_LOG("KeepAliveThread")->debug() << "CommandLoad::updateConfigFile get adapter " << itAdapters->first << endl;

            if (itAdapters->first == "")
            {
                continue;
            }

            tEp.parse(itAdapters->second.endpoint);

            {
                std::lock_guard<std::mutex> lock(_mutex);
                _allPorts.insert(tEp.getPort());
            }

            m["endpoint"] = tEp.toString();
            m["allow"]        = itAdapters->second.allowIp;
            m["queuecap"]     = TC_Common::tostr(itAdapters->second.queuecap);
            m["queuetimeout"] = TC_Common::tostr(itAdapters->second.queuetimeout);
            m["maxconns"]     = TC_Common::tostr(itAdapters->second.maxConnections);
            m["threads"]      = TC_Common::tostr(itAdapters->second.threadNum);
            m["servant"]      = TC_Common::tostr(itAdapters->second.servant);
            m["protocol"]     = itAdapters->second.protocol == "" ? "tars" : itAdapters->second.protocol;
            // m["handlegroup"]  = itAdapters->second.handlegroup == "" ? itAdapters->first : itAdapters->second.handlegroup;

            tConf.insertDomainParam("/tars/application/server/" + itAdapters->first, m, true);
        }

        //获取本地socket
        uint16_t p = tEp.getPort();
        TC_Endpoint tLocalEndpoint;
        try
        {
            //原始配置文件中有admin端口了, 直接使用
            TC_Config conf;
            conf.parseFile(_confFile);
            TC_Endpoint ep;
            ep.parse(conf.get("/tars/application/server<local>"));
            p = ep.getPort();            
        }
        catch(const std::exception& e)
        {
            //随机分配不冲突的端口
            for (;;)
            {
                p = 30000 + rand() % 15000;
                TC_Socket s;
                s.createSocket(SOCK_STREAM, AF_INET);
                int ret = s.connectNoThrow("127.0.0.1", p);

                if(ret != 0 && _allPorts.find(p) == _allPorts.end())
                {
                    //端口无法连接且之前没用过, 则使用之
                    break;
                }
            }
        }


        {
            std::lock_guard<std::mutex> lock(_mutex);
            _allPorts.insert(tEp.getPort());
        }


        // uint16_t p = tEp.getPort();
        // if (p == 0)
        // {
        //     try
        //     {
        //         //原始配置文件中有admin端口了, 直接使用
        //         TC_Config conf;
        //         conf.parseFile(_confFile);
        //         TC_Endpoint ep;
        //         ep.parse(conf.get("/tars/application/server<local>"));
        //         p = ep.getPort();
        //     }
        //     catch (exception& ex)
        //     {
        //         //随机分配一个端口
        //         TC_Socket t;
        //         t.createSocket();
        //         t.bind("127.0.0.1", 0);
        //         string d;
        //         t.getSockName(d, p);
        //         t.close();
        //     }

        // }

        tLocalEndpoint.setPort(p);
        tLocalEndpoint.setHost("127.0.0.1");
        tLocalEndpoint.setType(TC_Endpoint::TCP);
        tLocalEndpoint.setTimeout(10000);
	    // NODE_LOG("KeepAliveThread")->debug() << tConf.tostr() << endl;

        //需要宏替换部分配置
        TC_Config tConfMacro;
        map<string, string> mMacro;
        mMacro.clear();
        mMacro["locator"] = Application::getCommunicator()->getProperty("locator");

        //>>修改成从主控获取locator地址
        vector<tars::EndpointF> activeEp;
        vector<tars::EndpointF> inactiveEp;
        int iRet = 0;

        if(_succ)
        {
            QueryFPrx queryProxy = AdminProxy::getInstance()->getQueryProxy();
             try
            {
                iRet = queryProxy->findObjectById4All(AdminProxy::getInstance()->getQueryProxyName(), activeEp, inactiveEp);
                NODE_LOG("KeepAliveThread")->debug() << "CommandLoad::updateConfigFile " << _serverObjectPtr->getServerId() << "|iRet|" << iRet << "|" << activeEp.size() << "|" << inactiveEp.size() << endl;
            }
            catch (exception& e)
            {
                //获取主控地址异常时,仍使用node中的locator
                NODE_LOG("KeepAliveThread")->error() << "CommandLoad::updateConfigFile:get registry locator exception:" << e.what() << "|" << _serverObjectPtr->getServerId() << endl;
                iRet = -1;
            }
            catch (...)
            {
                NODE_LOG("KeepAliveThread")->error() << "CommandLoad::updateConfigFile:get registry locator unknown exception|" << _serverObjectPtr->getServerId() << endl;
                iRet = -1;
            }
        }

        if (iRet == 0 && activeEp.size() > 0)
        {
            string sLocator = AdminProxy::getInstance()->getQueryProxyName() + "@";
            for (size_t i = 0; i < activeEp.size(); ++i)
            {
                string sSingleAddr = "tcp -h " + activeEp[i].host + " -p " + TC_Common::tostr(activeEp[i].port);
                sLocator += sSingleAddr + ":";
            }
            sLocator = sLocator.substr(0, sLocator.length() - 1);
            mMacro["locator"] = sLocator;
	        NODE_LOG("KeepAliveThread")->debug() << "CommandLoad::updateConfigFile:" << _serverObjectPtr->getServerId() << "|locator|" << sLocator << endl;
        }

        mMacro["modulename"] = _desc.application + "." + _desc.serverName;
        mMacro["app"]        = _desc.application;
        mMacro["server"]     = _desc.serverName;
        mMacro["serverid"]   = _serverObjectPtr->getServerId();
        mMacro["localip"]    = g_app.getAdapterEndpoint("ServerAdapter").getHost();
        mMacro["exe"]        = TC_File::simplifyDirectory(_exeFile);
        mMacro["basepath"]   = TC_File::simplifyDirectory(_exePath) + FILE_SEP;
        mMacro["datapath"]   = TC_File::simplifyDirectory(_serverDir) + FILE_SEP + "data" + FILE_SEP;
        mMacro["logpath"]    = ServerConfig::LogPath;
        mMacro["local"]      = tLocalEndpoint.toString();

        mMacro["mainclass"]   = "com.qq." + TC_Common::lower(_desc.application) + "." + TC_Common::lower(_desc.serverName) + "." + _desc.serverName;
        mMacro["config-center-port"] = TC_Common::tostr(_desc.configCenterPort);

        mMacro["setdivision"] = _desc.setId;

        mMacro["enableset"]   = "n";
        if (!mMacro["setdivision"].empty())
        {
            mMacro["enableset"] = "y";
        }
        else
        {
            mMacro["setdivision"] = "NULL";
        }

        mMacro["asyncthread"] = TC_Common::tostr(_desc.asyncThreadNum);

        //创建目录
        TC_File::makeDirRecursive(mMacro["basepath"]);
        TC_File::makeDirRecursive(mMacro["datapath"]);
        TC_File::makeDirRecursive(_logPath + FILE_SEP + _desc.application + FILE_SEP + _desc.serverName + FILE_SEP);

        //合并两类配置
        _serverObjectPtr->setMacro(mMacro);

        string strProfile = _serverObjectPtr->decodeMacro(_desc.profile);
        tConfMacro.parseString(strProfile);

        tConf.joinConfig(tConfMacro, true);

        string sStream  = TC_Common::replace(tConf.tostr(), "\\s", " ");
        string sConfigFileBak = _confFile + ".bak";
        if (TC_File::isFileExist(_confFile) && TC_File::load2str(_confFile) != sStream)
        {
            TC_File::copyFile(_confFile, sConfigFileBak);
        }

	    NODE_LOG("KeepAliveThread")->debug() << "save to: " << _confFile << endl;

	    ofstream configfile(_confFile.c_str());
        if (!configfile.good())
        {
	        NODE_LOG("KeepAliveThread")->error() << "CommandLoad::updateConfigFile cannot create configuration file: " << _confFile << endl;
            return -1;
        }

        configfile << sStream;
        configfile.close();

	    _logPath       = tConf.get("/tars/application/server<logpath>", "");

        _serverObjectPtr->setJvmParams(tConf.get("/tars/application/server<jvmparams>", ""));
        _serverObjectPtr->setMainClass(tConf.get("/tars/application/server<mainclass>", ""));
        _serverObjectPtr->setClassPath(tConf.get("/tars/application/server<classpath>", ""));
        _serverObjectPtr->setEnv(tConf.get("/tars/application/server<env>", ""));
        _serverObjectPtr->setHeartTimeout(TC_Common::strto<int>(tConf.get("/tars/application/server<hearttimeout>", "")));
        _serverObjectPtr->setActivatingTimeout(TC_Common::strto<int>(tConf.get("/tars/application/server<activating-timeout>", "")));

        _serverObjectPtr->setRedirectPath(tConf.get("/tars/application/<redirectpath>", ""));

        _serverObjectPtr->setBackupFileNames(tConf.get("/tars/application/server<backupfiles>", "classes/autoconf"));

        bool bEn = (TC_Common::lower(tConf.get("/tars/application/server<enableworking>", "y")) == "y") ? true : false;

        _serverObjectPtr->setEnSynState(bEn);

        _serverObjectPtr->setExeFile(_exeFile);
        _serverObjectPtr->setConfigFile(_confFile);
        _serverObjectPtr->setExePath(_exePath);
        _serverObjectPtr->setLogPath(_logPath);
        _serverObjectPtr->setLibPath(_libPath);
        _serverObjectPtr->setServerDir(_serverDir);
        _serverObjectPtr->setNodeInfo(_nodeInfo);

        //取合并后配置
        TC_Endpoint localEp;
        localEp.parse(tConf.get("/tars/application/server<local>"));
        _serverObjectPtr->setLocalEndpoint(localEp);

        _serverObjectPtr->setServerType(_serverType);
        _serverObjectPtr->setScript(_startScript, _stopScript, _monitorScript);
        _serverObjectPtr->setLoaded(true);

        return 0;
    }
    catch (exception& e)
    {
        sResult = e.what();
	    NODE_LOG("KeepAliveThread")->error() << "CommandLoad::updateConfigFile "<<e.what()<<endl;
    }
    catch (...)
    {
	    NODE_LOG("KeepAliveThread")->error() << "CommandLoad::updateConfigFile  catch unkown erro"<<endl;
    }
    return -1;
}

//服务配置文件文件下载
void CommandLoad::getRemoteConf()
{
    if(_serverObjectPtr->getServerId() == "tars.tarsconfig")
    {
        return ;
    }

    string sResult;
    try
    {
        ConfigPrx pPtr = Application::getCommunicator()->stringToProxy<ConfigPrx>(ServerConfig::Config);
        vector<string> vf;
        int ret;

        if (_desc.setId.empty())
        {
            ret = pPtr->ListConfig(_desc.application, _desc.serverName, vf, ServerConfig::Context);
        }
        else
        {
            struct ConfigInfo confInfo;
            confInfo.appname = _desc.application;
            confInfo.servername = _desc.serverName;
            confInfo.setdivision = _desc.setId;
            ret = pPtr->ListConfigByInfo(confInfo, vf, ServerConfig::Context);
        }

        if (ret != 0)
        {
	        NODE_LOG("KeepAliveThread")->error() << "CommandLoad::getRemoteConf [fail] get remote file list"<< endl;
            g_app.reportServer(_serverObjectPtr->getServerId(), "", _serverObjectPtr->getNodeInfo().nodeName, sResult); 
            // g_app.reportServer( sResult);
        }

        for (unsigned i = 0; i < vf.size(); i++)
        {
            //脚本拉取  需要宏替换
            if (_serverObjectPtr->isScriptFile(vf[i]) == true)
            {
                _serverObjectPtr->getRemoteScript(vf[i]);
                continue;
            }

            //非tars服务配置文件需要node拉取 tars服务配置服务启动时自己拉取
            if (_serverObjectPtr->isTarsServer() != true)
            {
                RemoteConfig remoteConfig;
                remoteConfig.setConfigInfo(Application::getCommunicator(),ServerConfig::Config,_desc.application, _desc.serverName, _exePath,_desc.setId);
                remoteConfig.addConfig(vf[i], sResult);
                g_app.reportServer(_serverObjectPtr->getServerId(), "", _serverObjectPtr->getNodeInfo().nodeName, sResult); 
                // g_app.reportServer(_serverObjectPtr->getServerId(), sResult);
            }
        }
    }
    catch (exception& e)
    {
	    NODE_LOG("KeepAliveThread")->error() << "CommandLoad::getRemoteConf error:" << e.what() << endl;
    }
}

