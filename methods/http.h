// -*- mode: cpp; mode: fold -*-
// Description								/*{{{*/
/* ######################################################################

   HTTP Acquire Method - This is the HTTP acquire method for APT.

   ##################################################################### */
									/*}}}*/

#ifndef APT_HTTP_H
#define APT_HTTP_H

#include <apt-pkg/strutl.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <sys/time.h>

#include "basehttp.h"
#include "connect.h"

using std::cout;
using std::endl;

class FileFd;
class HttpMethod;
class Hashes;

class HttpMethod : public BaseHttpMethod
{
   public:
   virtual void SendReq(FetchItem *Itm) APT_OVERRIDE;

   virtual std::unique_ptr<ServerState> CreateServerState(URI const &uri) APT_OVERRIDE;
   virtual void RotateDNS() APT_OVERRIDE;
   virtual DealWithHeadersResult DealWithHeaders(FetchResult &Res, RequestState &Req) APT_OVERRIDE;

   protected:
   FileFd *File;
   std::string AutoDetectProxyCmd;

   public:
   friend struct HttpServerState;

   int Loop();

   explicit HttpMethod(std::string &&pProg);
};

#endif
