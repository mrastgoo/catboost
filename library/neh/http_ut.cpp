#include "http2.h"
#include "neh.h"
#include "rpc.h"

#include "factory.h"
#include "https.h"

#include <library/unittest/registar.h>
#include <library/unittest/env.h>

#include <util/generic/buffer.h>
#include <util/network/endpoint.h>
#include <util/network/socket.h>
#include <util/stream/str.h>
#include <util/string/builder.h>
#include <util/system/platform.h>

using namespace NNeh;

SIMPLE_UNIT_TEST_SUITE(NehHttp) {
    /**
        @brief  class with a method that responses to requests.
     */
    class TRequestServer {
    public:
        void ServeRequest(const IRequestRef& req) {
            TDataSaver responseData;
            Sleep(TDuration::MilliSeconds(FromString<int>(req->Data())));
            responseData << req->Data();
            auto* httpReq = dynamic_cast<IHttpRequest*>(req.Get());
            TString headers = "\r\nContent-Type: text/plain";
            httpReq->SendReply(responseData, headers);
            // Cerr << "SendReply:" << req->Data() << Endl;
        }
    };


    /**
        @brief Auxiliary struct for tests with info about running services.
     */
    struct TServ {
        IServicesRef Services;
        ui16 ServerPort;
    };

    /**
        Creates service for serving request.

        @return ptr to IServices, port and error if occured. Tests failes if server could not be created.
     */
    TServ CreateServices() {
        TServ serv;
        TString err;

        //in loop try run service (bind port)
        for (ui16 basePort = 20000; basePort < 40000; basePort += 100) {
            serv.Services = CreateLoop();
            try {
                serv.ServerPort = basePort;
                TStringStream addr;
                addr << "http://localhost:" << serv.ServerPort << "/pipeline";
                TRequestServer requestServer;
                serv.Services->Add(addr.Str(), requestServer);
                serv.Services->ForkLoop(16); //<< throw exception, if can not bind port
                break;
            } catch (...) {
                serv.Services.Destroy();
                err = CurrentExceptionMessage();
            }
        }

        UNIT_ASSERT_C(serv.Services.Get(), ~err);
        return serv;
    }

    SIMPLE_UNIT_TEST(TPipelineRequests) {
        TServ serv = CreateServices();

        // const TResolvedHost* host = CachedResolve(TResolveInfo("localhost", serv.ServerPort));
        TNetworkAddress addr("localhost", serv.ServerPort);
        TEndpoint ep(new NAddr::TAddrInfo(&*addr.Begin()));
        TSocketHolder s(socket(ep.SockAddr()->sa_family, SOCK_STREAM, 0));
        UNIT_ASSERT_C(s != INVALID_SOCKET, "can't create socket");
        const int errConnect = connect(s, ep.SockAddr(), (int)ep.SockAddrLen());
        UNIT_ASSERT_C(!errConnect, "can't connect socket");

        // build http requests/expected_responses
        TStringStream reqs;
        TStringStream expectedResponses;
        // first requests must has most big delay with respoding
        // (but server side must return responses in right order)
        for (int i = 500; i >= 0; i -= 50) {
            TString delay = ToString<int>(i); // response delay (millseconds)
            reqs << "GET /pipeline?" << delay << " HTTP/1.1\r\n"
                 << "\r\n";
            expectedResponses << "HTTP/1.1 200 Ok\r\n"
                << "Content-Length: "<< +delay << "\r\n"
                << "Connection: Keep-Alive\r\n"
                << "Content-Type: text/plain\r\n"
                << "\r\n"
                << delay;
        }
        // send requests compare responses with expected responses
        const ssize_t nSend = send(s, ~reqs, +reqs, 0);
        UNIT_ASSERT_C(nSend == ssize_t(+reqs), "can't write reqs to socket");
        TVector<char> resp(+expectedResponses);
        size_t expectedCntBytes = +expectedResponses;
        SetSocketTimeout(s, 10, 0); // use as watchdog
        while (expectedCntBytes) {
            const ssize_t nRecv = recv(s, &resp[+expectedResponses - expectedCntBytes], expectedCntBytes, 0);
            UNIT_ASSERT_C(nRecv > 0, "can't read data from socket");
            expectedCntBytes -= nRecv;
        }
        const TStringBuf responseBuf(~resp, +resp);
        UNIT_ASSERT_C(responseBuf == expectedResponses.Str(), TString("has unexpected responses: ") + responseBuf);
    }


    /**
        @brief  Tests that neh closes http/1.0 connection after repling to it.
     */
    SIMPLE_UNIT_TEST(TClosedHttp10Connection) {
        TServ serv = CreateServices();

        // const TResolvedHost* host = CachedResolve(TResolveInfo("localhost", serv.ServerPort));
        const TNetworkAddress addr("localhost", serv.ServerPort);
        const TEndpoint ep(new NAddr::TAddrInfo(&*addr.Begin()));

        // form request.
        TStringStream request;
        request << "GET /pipeline?0 HTTP/1.0\r\n"
            << "\r\n";

        // form etalon response.
        TStringStream expectedResponse;
        expectedResponse << "HTTP/1.0 200 Ok\r\n"
            << "Content-Length: 1\r\n"
            << "Content-Type: text/plain\r\n"
            << "\r\n"
            << "0";

        TSocketHolder s(socket(ep.SockAddr()->sa_family, SOCK_STREAM, 0));
        UNIT_ASSERT_C(s != INVALID_SOCKET, "can't create socket");
        SetSocketTimeout(s, 10, 0); // use as watchdog

        const int errConnect = connect(s, ep.SockAddr(), (int)ep.SockAddrLen());
        UNIT_ASSERT_C(!errConnect, "can't connect socket");

        const ssize_t nSend = send(s, ~request, +request, 0);
        UNIT_ASSERT_C(nSend == ssize_t(+request), "can't write request to socket.");
        TVector<char> response(+expectedResponse);
        size_t expectedCntBytes = +expectedResponse;
        while (expectedCntBytes) {
            const ssize_t nRecv = recv(s, &response[+expectedResponse - expectedCntBytes], expectedCntBytes, 0);
            UNIT_ASSERT_C(nRecv > 0, "can't read data from socket.");
            expectedCntBytes -= nRecv;
        }
        const TStringBuf responseBuf(~response, +response);
        UNIT_ASSERT_C(responseBuf == expectedResponse.Str(), TString("bad response: ") + responseBuf);

        /// Try to write to socket after waiting for a while to check that it's closed.
        Sleep(TDuration::MilliSeconds(500));

        // this test works fine.
        const int socket_fd = static_cast<int>(s);
        TBuffer buf(1);
        fd_set readset;
        FD_ZERO(&readset);
        FD_SET(static_cast<int>(s), &readset);
        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 500000;
        const int selret = select(socket_fd + 1, &readset, nullptr, nullptr, &tv);
        UNIT_ASSERT_C(selret != -1, "select failed");
        UNIT_ASSERT_C(selret == 1, "select should return one fd that is ready to return 0 on recv call");
        const ssize_t nReadBytes = recv(s, buf.Data(), buf.Size(), 0);
        UNIT_ASSERT_C(nReadBytes == 0, "connection must be closed, but we did not get 0 as return value from it.");
    }
}
