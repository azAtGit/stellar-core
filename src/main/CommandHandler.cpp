// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the ISC License. See the COPYING file at the top-level directory of
// this distribution or at http://opensource.org/licenses/ISC

#include "main/CommandHandler.h"
#include "main/Application.h"
#include "main/Config.h"
#include "lib/http/server.hpp"
#include "util/Logging.h"
#include "util/make_unique.h"
#include "medida/reporting/json_reporter.h"
#include "overlay/PeerMaster.h"
#include "crypto/Hex.h"
#include "xdrpp/marshal.h"
#include "herder/HerderGateway.h"


using std::placeholders::_1;
using std::placeholders::_2;

namespace stellar
{
CommandHandler::CommandHandler(Application& app) : mApp(app)
{
    if(!mApp.getConfig().RUN_STANDALONE && mApp.getConfig().HTTP_PORT)
    {
        std::string ipStr;
        ipStr = "127.0.0.1";
        LOG(INFO) << "Listening on " << ipStr << ":" << mApp.getConfig().HTTP_PORT
            << " for HTTP requests";

        mServer = stellar::make_unique<http::server::server>(
            app.getMainIOService(), ipStr, mApp.getConfig().HTTP_PORT);
    } else
    {
        mServer = stellar::make_unique<http::server::server>(app.getMainIOService());
    }

    mServer->addRoute("stop",
        std::bind(&CommandHandler::stop, this, _1, _2));
    mServer->addRoute("peers",
        std::bind(&CommandHandler::peers, this, _1, _2));
    mServer->addRoute("info",
        std::bind(&CommandHandler::info, this, _1, _2));
    mServer->addRoute("metrics",
        std::bind(&CommandHandler::metrics, this, _1, _2));
    mServer->addRoute("reload_cfg",
        std::bind(&CommandHandler::reloadCfg, this, _1, _2));
    mServer->addRoute("logrotate",
        std::bind(&CommandHandler::logRotate, this, _1, _2));
    mServer->addRoute("connect",
        std::bind(&CommandHandler::connect, this, _1, _2));
    mServer->addRoute("tx", std::bind(&CommandHandler::tx, this, _1, _2));
    mServer->addRoute("ll", std::bind(&CommandHandler::ll, this, _1, _2));
   
}

void CommandHandler::manualCmd(const std::string& cmd)
{
    http::server::reply reply;
    http::server::request request;
    request.uri = cmd;
    mServer->handle_request(request, reply);
    LOG(INFO) << cmd << " -> " << reply.content;
}

void
CommandHandler::stop(const std::string& params, std::string& retStr)
{
    retStr = "Stopping...";
    mApp.gracefulStop();
}

void
CommandHandler::peers(const std::string& params, std::string& retStr)
{
    retStr = "Peers...";
}

void
CommandHandler::info(const std::string& params, std::string& retStr)
{
    retStr = "Info...";
}

void
CommandHandler::metrics(const std::string& params, std::string& retStr)
{
    medida::reporting::JsonReporter jr(mApp.getMetrics());
    retStr = jr.Report();
}

void
CommandHandler::reloadCfg(const std::string& params, std::string& retStr)
{
    std::string filename = params.substr(6);
    if (filename.size())
    {
        retStr = "Loading new Config file";
        // GRAYDON: do we want to call this from some other thread?
        // mApp.mConfig.load(filename);
    }
    else
    {
        retStr = "Must specify a filename: reload_cfg&file=????";
    }
}

void
CommandHandler::logRotate(const std::string& params, std::string& retStr)
{
    retStr = "Log rotate...";
}

void
CommandHandler::connect(const std::string& params, std::string& retStr)
{
    std::string addr = params.substr(6);
    if(addr.size())
    {
        retStr = "Connect to";
        mApp.getPeerMaster().connectTo(addr);
    } else
    {
        retStr = "Must specify a peer: connect&peer=????";
    }
}


// "Must specify a log level: ll?level=<level>&partition=<name>";
void CommandHandler::ll(const std::string& params, std::string& retStr)
{
    std::map<std::string, std::string> retMap;
    http::server::server::parseParams(params, retMap);
    el::Level level = Logging::getLLfromString(retMap["level"]);
    std::string partition = retMap["partition"];
    Logging::setLogLevel(level, partition.c_str());
    retStr = "Log level set";
}

void
CommandHandler::tx(const std::string& params, std::string& retStr)
{
    std::ostringstream output;

    const std::string prefix("?blob=");
    if (params.compare(0, prefix.size(), prefix) == 0)
    {
        TransactionEnvelope envelope;
        try
        {
            std::string blob = params.substr(prefix.size());
            std::vector<uint8_t> binBlob = hexToBin(blob);

            xdr::xdr_from_opaque(binBlob, envelope);
            TransactionFramePtr transaction =
                TransactionFrame::makeTransactionFromWire(envelope);
            if(transaction)
            {
                // add it to our current set
                // and make sure it is valid
                if(mApp.getHerderGateway().recvTransaction(transaction))
                {
                    StellarMessage msg;
                    msg.type(TRANSACTION);
                    msg.transaction() = envelope;
                    mApp.getOverlayGateway().broadcastMessage(msg);
                }

                std::string resultHex = binToHex(xdr::xdr_to_msg(transaction->getResult()));
                output << "{\"result\": \"" << resultHex << "\"}";
            }
        }
        catch (std::exception &e)
        {
            output << "{\"exception\": \"" << e.what() << "\"}";
        }
        catch(...)
        {
            output << "{\"exception\": \"generic\"}";
        }
    }
    else
    {
        output << "{\"exception\": \"Must specify a tx blob: tx?blob=<tx in xdr format>\"}";
    }

    retStr = output.str();
}
}
