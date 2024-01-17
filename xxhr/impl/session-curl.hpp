#ifndef XXHR_DETAIL_SESSION_CURL_HPP
#define XXHR_DETAIL_SESSION_CURL_HPP

//TODO: impement asio cURL instead of easy-perform https://github.com/RobertLeahy/ASIO-cURL

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <mutex>
#include <array>

#include <boost/predef.h>

#include <curl/curl.h>


#include <xxhr/auth.hpp>
#include <xxhr/body.hpp>
#include <xxhr/cookies.hpp>
#include <xxhr/xxhrtypes.hpp>
#include <xxhr/digest.hpp>
#include <xxhr/bearer.hpp>
#include <xxhr/max_redirects.hpp>
#include <xxhr/multipart.hpp>
#include <xxhr/parameters.hpp>
#include <xxhr/response.hpp>
#include <xxhr/timeout.hpp>
#include <xxhr/proxies.hpp>
#include <xxhr/proxyauth.hpp>



namespace xxhr {
  // Ignored here since libcurl reqires a long:
  // NOLINTNEXTLINE(google-runtime-int)
  constexpr long ON = 1L;
  // Ignored here since libcurl reqires a long:
  // NOLINTNEXTLINE(google-runtime-int)
  constexpr long OFF = 0L;

  struct CurlHolder {
    private:
      /**
       * Mutex for curl_easy_init().
       * curl_easy_init() is not thread save.
       * References:
       * https://curl.haxx.se/libcurl/c/curl_easy_init.html
       * https://curl.haxx.se/libcurl/c/threadsafe.html
       **/
      // It does not make sense to make a std::mutex const.
      // NOLINTNEXTLINE (cppcoreguidelines-avoid-non-const-global-variables)    
      static inline std::mutex curl_easy_init_mutex_;

    public:
      CURL* handle{nullptr};
      struct curl_slist* chunk{nullptr};
      struct curl_httppost* formpost{nullptr};
      std::array<char, CURL_ERROR_SIZE> error{};

      CurlHolder() {
          /**
           * Allow multithreaded access to CPR by locking curl_easy_init().
           * curl_easy_init() is not thread safe.
           * References:
           * https://curl.haxx.se/libcurl/c/curl_easy_init.html
           * https://curl.haxx.se/libcurl/c/threadsafe.html
           **/
          curl_easy_init_mutex_.lock();
          handle = curl_easy_init();
          curl_easy_init_mutex_.unlock();

          assert(handle);
      } // namespace cpr

      CurlHolder(const CurlHolder& other) = default;
      CurlHolder(CurlHolder&& old) noexcept = default;
      ~CurlHolder() {
          curl_slist_free_all(chunk);
          curl_formfree(formpost);
          curl_easy_cleanup(handle);
      }

      CurlHolder& operator=(CurlHolder&& old) noexcept = default;
      CurlHolder& operator=(const CurlHolder& other) = default;
  }; 

  class Session::Impl  : public std::enable_shared_from_this<Session::Impl> {
    public:
      inline Impl();

      inline void SetUrl(const Url& url);
      inline void SetParameters(const Parameters& parameters);
      inline void SetParameters(Parameters&& parameters);
      inline void SetHeader(const Header& header);
      inline void SetTimeout(const Timeout& timeout);
      inline void SetAuth(const Authentication& auth);
      inline void SetDigest(const Digest& auth);
      inline void SetBearer(const Bearer& token);

      inline void SetProxies(Proxies&& proxies);
      inline void SetProxies(const Proxies& proxies);
      inline void SetProxyAuth(ProxyAuthentication&& proxy_auth);
      inline void SetProxyAuth(const ProxyAuthentication& proxy_auth);
      inline void SetMultipart(Multipart&& multipart);
      inline void SetMultipart(const Multipart& multipart);
      inline void SetNTLM(const NTLM& auth);
      inline void SetRedirect(const bool& redirect);
      inline void SetMaxRedirects(const MaxRedirects& max_redirects);
      inline void SetCookies(const Cookies& cookies);

      inline void SetBody(const Body& body);
      inline void SetDownloadTarget(const DownloadTo& download_to);

      template<class Handler>
      inline void SetHandler(const on_response_<Handler>&& functor) {
        on_response = functor;
      }

      inline void DELETE_();
      inline void GET();
      inline void HEAD();
      inline void OPTIONS();
      inline void PATCH();
      inline void POST();
      inline void PUT();

      inline std::shared_ptr<CurlHolder> GetCurlHolder();

      inline void PrepareDelete();
      inline void PrepareGet();
      inline void PrepareHead();
      inline void PrepareOptions();
      inline void PreparePatch();
      inline void PreparePost();
      inline void PreparePut();
      inline Response Complete(CURLcode curl_error);

    private:
      std::function<void(Response&&)> on_response;

      inline void SetHeaderInternal();
      bool hasBodyOrPayload_{false};

      std::shared_ptr<CurlHolder> curl_;
      Url url_;
      Parameters parameters_;
      Proxies proxies_;
      ProxyAuthentication proxyAuth_;
      Header header_;
      
      bool has_download_target_{false};
      DownloadTo download_target_;
      util::named_ofstream download_stream_;

      /**
       * Will be set by the read callback.
       * Ensures that the "Transfer-Encoding" is set to "chunked", if not overriden in header_.
       **/
      bool chunkedTransferEncoding{false};

      std::string response_string_;
      std::string header_string_;

      inline Cookies parseCookies(curl_slist* raw_cookies) {
        Cookies cookies;
        for (curl_slist* nc = raw_cookies; nc; nc = nc->next) {
            std::vector<std::string> tokens = util::split(nc->data, '\t');
            std::string value = tokens.back();
            tokens.pop_back();
            cookies[tokens.back()] = value;
        }
        return cookies;
      }

      inline ErrorCode getErrorCodeForCurlError(std::int32_t curl_code) {
        switch (curl_code) {
            case CURLE_OK:
                return ErrorCode::OK;
            case CURLE_UNSUPPORTED_PROTOCOL:
                return ErrorCode::UNSUPPORTED_PROTOCOL;
            case CURLE_URL_MALFORMAT:
                return ErrorCode::INVALID_URL_FORMAT;
            case CURLE_COULDNT_RESOLVE_PROXY:
                return ErrorCode::PROXY_RESOLUTION_FAILURE;
            case CURLE_COULDNT_RESOLVE_HOST:
                return ErrorCode::HOST_RESOLUTION_FAILURE;
            case CURLE_COULDNT_CONNECT:
                return ErrorCode::CONNECTION_FAILURE;
            case CURLE_OPERATION_TIMEDOUT:
                return ErrorCode::TIMEDOUT;
            case CURLE_SSL_CONNECT_ERROR:
                return ErrorCode::SSL_CONNECT_ERROR;
    #if LIBCURL_VERSION_NUM < 0x073e00
            case CURLE_PEER_FAILED_VERIFICATION:
                return ErrorCode::SSL_REMOTE_CERTIFICATE_ERROR;
    #endif
            case CURLE_ABORTED_BY_CALLBACK:
            case CURLE_WRITE_ERROR:
                return ErrorCode::REQUEST_CANCELLED;
            case CURLE_GOT_NOTHING:
                return ErrorCode::EMPTY_RESPONSE;
            case CURLE_SSL_ENGINE_NOTFOUND:
            case CURLE_SSL_ENGINE_SETFAILED:
                return ErrorCode::GENERIC_SSL_ERROR;
            case CURLE_SEND_ERROR:
                return ErrorCode::NETWORK_SEND_FAILURE;
            case CURLE_RECV_ERROR:
                return ErrorCode::NETWORK_RECEIVE_ERROR;
            case CURLE_SSL_CERTPROBLEM:
                return ErrorCode::SSL_LOCAL_CERTIFICATE_ERROR;
            case CURLE_SSL_CIPHER:
                return ErrorCode::GENERIC_SSL_ERROR;
    #if LIBCURL_VERSION_NUM >= 0x073e00
            case CURLE_PEER_FAILED_VERIFICATION:
                return ErrorCode::SSL_REMOTE_CERTIFICATE_ERROR;
    #else
            case CURLE_SSL_CACERT:
                return ErrorCode::SSL_CACERT_ERROR;
    #endif
            case CURLE_USE_SSL_FAILED:
            case CURLE_SSL_ENGINE_INITFAILED:
                return ErrorCode::GENERIC_SSL_ERROR;
            case CURLE_SSL_CACERT_BADFILE:
                return ErrorCode::SSL_CACERT_ERROR;
            case CURLE_SSL_SHUTDOWN_FAILED:
                return ErrorCode::GENERIC_SSL_ERROR;
            case CURLE_SSL_CRL_BADFILE:
            case CURLE_SSL_ISSUER_ERROR:
                return ErrorCode::SSL_CACERT_ERROR;
            case CURLE_TOO_MANY_REDIRECTS:
                return ErrorCode::TOO_MANY_REDIRECTS;
            default:
                return ErrorCode::INTERNAL_ERROR;
        }
      }

      //Response makeDownloadRequest();
      inline Response makeRequest();
      inline void prepareCommon();
  };

Session::Impl::Impl() : curl_(std::make_shared<CurlHolder>()) {

  // Set up some sensible defaults
  if (std::getenv("VERBOSE")!= nullptr) {
    curl_easy_setopt(curl_->handle, CURLOPT_VERBOSE, ON);
  } else {
    curl_easy_setopt(curl_->handle, CURLOPT_VERBOSE, OFF);
  }

  curl_version_info_data* version_info = curl_version_info(CURLVERSION_NOW);
  std::string version = "curl/" + std::string{version_info->version};
  curl_easy_setopt(curl_->handle, CURLOPT_USERAGENT, version.c_str());
  curl_easy_setopt(curl_->handle, CURLOPT_NOPROGRESS, 1L);
  curl_easy_setopt(curl_->handle, CURLOPT_ERRORBUFFER, curl_->error.data());
  curl_easy_setopt(curl_->handle, CURLOPT_COOKIEFILE, "");
  //curl_easy_setopt(curl_->handle, CURLOPT_NOSIGNAL, 1L);

  // Folow all redirects inconditionally 
  curl_easy_setopt(curl_->handle, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl_->handle, CURLOPT_MAXREDIRS, std::numeric_limits<std::int32_t>::max());
  curl_easy_setopt(curl_->handle, CURLOPT_UNRESTRICTED_AUTH, 1L);

  SetRedirect(true);

  curl_easy_setopt(curl_->handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_NONE);

  curl_easy_setopt(curl_->handle, CURLOPT_SSL_VERIFYPEER, ON);
  curl_easy_setopt(curl_->handle, CURLOPT_SSL_VERIFYHOST, 2L);

#if LIBCURL_VERSION_NUM >= 0x071900
  curl_easy_setopt(curl_->handle, CURLOPT_TCP_KEEPALIVE, 1L);
#endif
}

void Session::Impl::SetUrl(const Url& url) {
    url_ = url;
}

void Session::Impl::SetParameters(const Parameters& parameters) {
    parameters_ = parameters;
}

void Session::Impl::SetParameters(Parameters&& parameters) {
    parameters_ = std::move(parameters);
}

void Session::Impl::SetHeaderInternal() {
    curl_slist* chunk = nullptr;
    for (const std::pair<const std::string, std::string>& item : header_) {
        std::string header_string = item.first;
        if (item.second.empty()) {
            header_string += ";";
        } else {
            header_string += ": " + item.second;
        }

        curl_slist* temp = curl_slist_append(chunk, header_string.c_str());
        if (temp) {
            chunk = temp;
        }
    }

    // Set the chunked transfer encoding in case it does not already exist:
    if (chunkedTransferEncoding && header_.find("Transfer-Encoding") == header_.end()) {
        curl_slist* temp = curl_slist_append(chunk, "Transfer-Encoding:chunked");
        if (temp) {
            chunk = temp;
        }
    }

    curl_easy_setopt(curl_->handle, CURLOPT_HTTPHEADER, chunk);

    curl_slist_free_all(curl_->chunk);
    curl_->chunk = chunk;
}

void Session::Impl::SetHeader(const Header& header) {
    header_ = header;
}

void Session::Impl::SetTimeout(const Timeout& timeout) {
    curl_easy_setopt(curl_->handle, CURLOPT_TIMEOUT_MS, timeout.ms.count());
    curl_easy_setopt(curl_->handle, CURLOPT_CONNECTTIMEOUT_MS, timeout.ms.count());
}

void Session::Impl::SetAuth(const Authentication& auth) {
    // Ignore here since this has been defined by libcurl.
    curl_easy_setopt(curl_->handle, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    curl_easy_setopt(curl_->handle, CURLOPT_USERPWD, auth.GetAuthString());
}

void Session::Impl::SetBearer(const Bearer& token) {
    //TODO: do it with SetHeader
    // Ignore here since this has been defined by libcurl.
    //curl_easy_setopt(curl_->handle, CURLOPT_HTTPAUTH, CURLAUTH_BEARER);
    //curl_easy_setopt(curl_->handle, CURLOPT_XOAUTH2_BEARER, token.GetToken());
}

void Session::Impl::SetDigest(const Digest& auth) {
    // Ignore here since this has been defined by libcurl.
    curl_easy_setopt(curl_->handle, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
    curl_easy_setopt(curl_->handle, CURLOPT_USERPWD, auth.GetAuthString());
}

void Session::Impl::SetProxies(const Proxies& proxies) {
    proxies_ = proxies;
}

void Session::Impl::SetProxies(Proxies&& proxies) {
    proxies_ = std::move(proxies);
}

void Session::Impl::SetProxyAuth(ProxyAuthentication&& proxy_auth) {
    proxyAuth_ = std::move(proxy_auth);
}

void Session::Impl::SetProxyAuth(const ProxyAuthentication& proxy_auth) {
    proxyAuth_ = proxy_auth;
}

void Session::Impl::SetMultipart(Multipart&& multipart) {
    curl_httppost* formpost = nullptr;
    curl_httppost* lastptr = nullptr;

    for (const Part& part : multipart.parts) {
        std::vector<curl_forms> formdata;
        if (part.is_buffer) {
            // Do not use formdata, to prevent having to use reinterpreter_cast:
            curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, part.name.c_str(), CURLFORM_BUFFER, part.value.c_str(), CURLFORM_BUFFERPTR, part.data, CURLFORM_BUFFERLENGTH, part.datalen, CURLFORM_END);
        } else {
            formdata.push_back({CURLFORM_COPYNAME, part.name.c_str()});
            if (part.is_file) {
                formdata.push_back({CURLFORM_FILE, part.value.c_str()});
            } else {
                formdata.push_back({CURLFORM_COPYCONTENTS, part.value.c_str()});
            }
        }
        if (!part.content_type.empty()) {
            formdata.push_back({CURLFORM_CONTENTTYPE, part.content_type.c_str()});
        }

        formdata.push_back({CURLFORM_END, nullptr});
        curl_formadd(&formpost, &lastptr, CURLFORM_ARRAY, formdata.data(), CURLFORM_END);
    }
    curl_easy_setopt(curl_->handle, CURLOPT_HTTPPOST, formpost);
    hasBodyOrPayload_ = true;

    curl_formfree(curl_->formpost);
    curl_->formpost = formpost;
}

void Session::Impl::SetMultipart(const Multipart& multipart) {
    curl_httppost* formpost = nullptr;
    curl_httppost* lastptr = nullptr;

    for (const Part& part : multipart.parts) {
        std::vector<curl_forms> formdata;
        if (part.is_buffer) {
            // Do not use formdata, to prevent having to use reinterpreter_cast:
            curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, part.name.c_str(), CURLFORM_BUFFER, part.value.c_str(), CURLFORM_BUFFERPTR, part.data, CURLFORM_BUFFERLENGTH, part.datalen, CURLFORM_END);
        } else {
            formdata.push_back({CURLFORM_COPYNAME, part.name.c_str()});
            if (part.is_file) {
                formdata.push_back({CURLFORM_FILE, part.value.c_str()});
            } else {
                formdata.push_back({CURLFORM_COPYCONTENTS, part.value.c_str()});
            }
        }
        if (!part.content_type.empty()) {
            formdata.push_back({CURLFORM_CONTENTTYPE, part.content_type.c_str()});
        }

        formdata.push_back({CURLFORM_END, nullptr});
        curl_formadd(&formpost, &lastptr, CURLFORM_ARRAY, formdata.data(), CURLFORM_END);
    }
    curl_easy_setopt(curl_->handle, CURLOPT_HTTPPOST, formpost);
    hasBodyOrPayload_ = true;

    curl_formfree(curl_->formpost);
    curl_->formpost = formpost;
}

void Session::Impl::SetNTLM(const NTLM& auth) {
  // Ignore here since this has been defined by libcurl.
  curl_easy_setopt(curl_->handle, CURLOPT_HTTPAUTH, CURLAUTH_NTLM);
  curl_easy_setopt(curl_->handle, CURLOPT_USERPWD, auth.GetAuthString());
}

void Session::Impl::SetRedirect(const bool& redirect) {
  curl_easy_setopt(curl_->handle, CURLOPT_POSTREDIR, (redirect) ?  CURL_REDIR_POST_ALL : 0L);
}

void Session::Impl::SetMaxRedirects(const MaxRedirects& redirect) {
  curl_easy_setopt(curl_->handle, CURLOPT_MAXREDIRS, redirect.number_of_redirects);
}

void Session::Impl::SetCookies(const Cookies& cookies) {
  curl_easy_setopt(curl_->handle, CURLOPT_COOKIELIST, "ALL");
  curl_easy_setopt(curl_->handle, CURLOPT_COOKIE, cookies.GetEncoded().c_str());
}

void Session::Impl::SetBody(const Body& body) {
  hasBodyOrPayload_ = true;
  curl_easy_setopt(curl_->handle, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.content.length()));
  curl_easy_setopt(curl_->handle, CURLOPT_POSTFIELDS, body.content.c_str());
}

void Session::Impl::PrepareDelete() {
    curl_easy_setopt(curl_->handle, CURLOPT_HTTPGET, 0L);
    curl_easy_setopt(curl_->handle, CURLOPT_NOBODY, 0L);
    curl_easy_setopt(curl_->handle, CURLOPT_CUSTOMREQUEST, "DELETE");
    prepareCommon();
}

void Session::Impl::DELETE_() {
    PrepareDelete();
    on_response(makeRequest());
}

void Session::Impl::SetDownloadTarget(const DownloadTo& download_to) {


    has_download_target_ = true;
    download_target_ = download_to;

    // create the output file(stream)
    download_stream_ = util::named_ofstream{download_to.destination_path};

    // actual file downloading / write to disk stuff
    curl_easy_setopt(curl_->handle, CURLOPT_WRITEFUNCTION, util::writeFileFunction);
    curl_easy_setopt(curl_->handle, CURLOPT_WRITEDATA, &download_stream_);

    // progress monitor
    if(download_target_.on_progress) {
      curl_easy_setopt(curl_->handle, CURLOPT_NOPROGRESS, 0);
      curl_easy_setopt(curl_->handle, CURLOPT_PROGRESSDATA, &download_target_);
      curl_easy_setopt(curl_->handle, CURLOPT_PROGRESSFUNCTION, +[](void* payload, double dltotal, double dlnow, double ultotal, double ulnow) {

        DownloadTo *dlto = static_cast<DownloadTo *>(payload);  // <- a ptr to download_target_ basically / no capturing in C callback :/
        dlto->on_progress(static_cast<size_t>(dltotal), static_cast<size_t>(dlnow));
        return 0; // "all good"
      });
    }    

    //
    on_response = [&](Response &&response) {      
      size_t ofstream_position = download_stream_.tellp();

      download_stream_.flush();
      download_stream_.close();
      download_to.download_finished(response, download_to.destination_path, ofstream_position);
    };
}

void Session::Impl::PrepareGet() {
    // In case there is a body or payload for this request, we create a custom GET-Request since a
    // GET-Request with body is based on the HTTP RFC **not** a leagal request.
    if (hasBodyOrPayload_) {
        curl_easy_setopt(curl_->handle, CURLOPT_NOBODY, 0L);
        curl_easy_setopt(curl_->handle, CURLOPT_CUSTOMREQUEST, "GET");
    } else {
        curl_easy_setopt(curl_->handle, CURLOPT_NOBODY, 0L);
        curl_easy_setopt(curl_->handle, CURLOPT_CUSTOMREQUEST, nullptr);
        curl_easy_setopt(curl_->handle, CURLOPT_HTTPGET, 1L);
    }
    prepareCommon();
}

void Session::Impl::GET() {
    PrepareGet();
    on_response(makeRequest());
}

void Session::Impl::PrepareHead() {
    curl_easy_setopt(curl_->handle, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl_->handle, CURLOPT_CUSTOMREQUEST, nullptr);
    prepareCommon();
}

void Session::Impl::HEAD() {
    PrepareHead();
    on_response(makeRequest());
}

void Session::Impl::PrepareOptions() {
    curl_easy_setopt(curl_->handle, CURLOPT_NOBODY, 0L);
    curl_easy_setopt(curl_->handle, CURLOPT_CUSTOMREQUEST, "OPTIONS");
    prepareCommon();
}

void Session::Impl::OPTIONS() {
    PrepareOptions();
    on_response(makeRequest());
}

void Session::Impl::PreparePatch() {
    curl_easy_setopt(curl_->handle, CURLOPT_NOBODY, 0L);
    curl_easy_setopt(curl_->handle, CURLOPT_CUSTOMREQUEST, "PATCH");
    prepareCommon();
}

void Session::Impl::PATCH() {
    PreparePatch();
    on_response(makeRequest());
}

void Session::Impl::PreparePost() {
    curl_easy_setopt(curl_->handle, CURLOPT_NOBODY, 0L);

    // In case there is no body or payload set it to an empty post:
    if (hasBodyOrPayload_) {
        curl_easy_setopt(curl_->handle, CURLOPT_CUSTOMREQUEST, nullptr);
    } else {
        curl_easy_setopt(curl_->handle, CURLOPT_POSTFIELDS, "");
        curl_easy_setopt(curl_->handle, CURLOPT_CUSTOMREQUEST, "POST");
    }
    prepareCommon();
}

void Session::Impl::POST() {
    PreparePost();
    on_response(makeRequest());
}

void Session::Impl::PreparePut() {
    curl_easy_setopt(curl_->handle, CURLOPT_NOBODY, 0L);
    curl_easy_setopt(curl_->handle, CURLOPT_CUSTOMREQUEST, "PUT");
    prepareCommon();
}

void Session::Impl::PUT() {
    PreparePut();
    on_response(makeRequest());
}

std::shared_ptr<CurlHolder> Session::Impl::GetCurlHolder() {
    return curl_;
}

void Session::Impl::prepareCommon() {
    assert(curl_->handle);

    // Set Header:
    SetHeaderInternal();

    const std::string parametersContent = parameters_.content;
    if (!parametersContent.empty()) {
        Url new_url{url_ + "?" + parametersContent};
        curl_easy_setopt(curl_->handle, CURLOPT_URL, new_url.c_str());
    } else {
        curl_easy_setopt(curl_->handle, CURLOPT_URL, url_.c_str());
    }

    // Proxy:
    std::string protocol = url_.substr(0, url_.find(':'));
    if (proxies_.has(protocol)) {
        curl_easy_setopt(curl_->handle, CURLOPT_PROXY, proxies_[protocol].c_str());
        if (proxyAuth_.has(protocol)) {
            curl_easy_setopt(curl_->handle, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
            curl_easy_setopt(curl_->handle, CURLOPT_PROXYUSERPWD, proxyAuth_[protocol]);
        }
    }

#if LIBCURL_VERSION_MAJOR >= 7
#if LIBCURL_VERSION_MINOR >= 21
    /* enable all supported built-in compressions */
    curl_easy_setopt(curl_->handle, CURLOPT_ACCEPT_ENCODING, nullptr);
#endif
#endif

#if BOOST_OS_MACOS
    curl_easy_setopt(curl_->handle, CURLOPT_CAINFO, "/etc/ssl/cert.pem");
#endif

    if (std::getenv("SSL_CERT_FILE") != nullptr) {
        std::string cacert_path = getenv("SSL_CERT_FILE");
        curl_easy_setopt(curl_->handle, CURLOPT_CAINFO,cacert_path.data());
    }

#if LIBCURL_VERSION_MAJOR >= 7
#if LIBCURL_VERSION_MINOR >= 71
    // Fix loading certs from Windows cert store when using OpenSSL:
    curl_easy_setopt(curl_->handle, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
#endif
#endif


    curl_->error[0] = '\0';

    if(!has_download_target_) {
      response_string_.clear();
      curl_easy_setopt(curl_->handle, CURLOPT_WRITEFUNCTION, util::writeFunction);
      curl_easy_setopt(curl_->handle, CURLOPT_WRITEDATA, &response_string_);
    }

    header_string_.clear();
    curl_easy_setopt(curl_->handle, CURLOPT_HEADERFUNCTION, util::writeFunction);
    curl_easy_setopt(curl_->handle, CURLOPT_HEADERDATA, &header_string_);

    // Enable so we are able to retrive certificate information:
    curl_easy_setopt(curl_->handle, CURLOPT_CERTINFO, 1L);
}

Response Session::Impl::makeRequest() {
    CURLcode curl_error = curl_easy_perform(curl_->handle);
    return Complete(curl_error);
}

Response Session::Impl::Complete(CURLcode curl_error) {
    curl_slist* raw_cookies{nullptr};
    curl_easy_getinfo(curl_->handle, CURLINFO_COOKIELIST, &raw_cookies);
    Cookies cookies = parseCookies(raw_cookies);
    curl_slist_free_all(raw_cookies);

    // Reset the has no body property:
    hasBodyOrPayload_ = false;
    long status_code{};
    curl_easy_getinfo(curl_->handle, CURLINFO_RESPONSE_CODE, &status_code);
    return Response(
      static_cast<std::int32_t>(status_code),
      Error(getErrorCodeForCurlError(curl_error)),
      std::move(response_string_), 
      util::parseHeader(header_string_), 
      url_,
      std::move(cookies));
}

Session::Session() : pimpl_{ std::make_shared<Impl>() } {}
Session::~Session() = default;
void Session::SetUrl(const Url& url) { pimpl_->SetUrl(url); }
void Session::SetParameters(const Parameters& parameters) { pimpl_->SetParameters(parameters); }
void Session::SetParameters(Parameters&& parameters) { pimpl_->SetParameters(std::move(parameters)); }
void Session::SetHeader(const Header& header) { pimpl_->SetHeader(header); }
void Session::SetTimeout(const Timeout& timeout) { pimpl_->SetTimeout(timeout); }
void Session::SetAuth(const Authentication& auth) { pimpl_->SetAuth(auth); }
void Session::SetDigest(const Digest& auth) { pimpl_->SetDigest(auth); }
//void Session::SetUserAgent(const UserAgent& ua) { pimpl_->SetUserAgent(ua); }

void Session::SetProxies(const Proxies& proxies) { pimpl_->SetProxies(proxies); }
void Session::SetProxies(Proxies&& proxies) { pimpl_->SetProxies(std::move(proxies)); }
void Session::SetProxyAuth(ProxyAuthentication&& proxy_auth) { pimpl_->SetProxyAuth(std::move(proxy_auth)); }
void Session::SetProxyAuth(const ProxyAuthentication& proxy_auth) { pimpl_->SetProxyAuth(proxy_auth); }
void Session::SetMultipart(const Multipart& multipart) { pimpl_->SetMultipart(multipart); }
void Session::SetMultipart(Multipart&& multipart) { pimpl_->SetMultipart(std::move(multipart)); }
void Session::SetNTLM(const NTLM& auth) { pimpl_->SetNTLM(auth); }
void Session::SetRedirect(const bool& redirect) { pimpl_->SetRedirect(redirect); }
void Session::SetCookies(const Cookies& cookies) { pimpl_->SetCookies(cookies); }
void Session::SetBody(const Body& body) { pimpl_->SetBody(body); }
void Session::SetBody(Body&& body) { pimpl_->SetBody(std::move(body)); }
void Session::SetDownloadTarget(const DownloadTo& download_to) { pimpl_->SetDownloadTarget(download_to); }
void Session::SetDownloadTarget(DownloadTo&& download_to) { pimpl_->SetDownloadTarget(std::move(download_to)); }


void Session::SetOption(const Url& url) { pimpl_->SetUrl(url); }
void Session::SetOption(const Parameters& parameters) { pimpl_->SetParameters(parameters); }
void Session::SetOption(Parameters&& parameters) { pimpl_->SetParameters(std::move(parameters)); }
void Session::SetOption(const Header& header) { pimpl_->SetHeader(header); }
void Session::SetOption(const Timeout& timeout) { pimpl_->SetTimeout(timeout); }
void Session::SetOption(const Authentication& auth) { pimpl_->SetAuth(auth); }
void Session::SetOption(const Digest& auth) { pimpl_->SetDigest(auth); }
void Session::SetOption(const Bearer& auth) { pimpl_->SetBearer(auth); }
//void Session::SetOption(const UserAgent& ua) { pimpl_->SetUserAgent(ua); }
void Session::SetOption(const Proxies& proxies) { pimpl_->SetProxies(proxies); }
void Session::SetOption(Proxies&& proxies) { pimpl_->SetProxies(std::move(proxies)); }
void Session::SetOption(ProxyAuthentication&& proxy_auth) { pimpl_->SetProxyAuth(std::move(proxy_auth)); }
void Session::SetOption(const ProxyAuthentication& proxy_auth) { pimpl_->SetProxyAuth(proxy_auth); }
void Session::SetOption(const Multipart& multipart) { pimpl_->SetMultipart(multipart); }
void Session::SetOption(Multipart&& multipart) { pimpl_->SetMultipart(std::move(multipart)); }
void Session::SetOption(const NTLM& auth) { pimpl_->SetNTLM(auth); }
void Session::SetOption(const bool& redirect) { pimpl_->SetRedirect(redirect); }
void Session::SetOption(const MaxRedirects& max_redirects) { pimpl_->SetMaxRedirects(max_redirects); }
void Session::SetOption(const Cookies& cookies) { pimpl_->SetCookies(cookies); }
void Session::SetOption(const Body& body) { pimpl_->SetBody(body); }
void Session::SetOption(Body&& body) { pimpl_->SetBody(std::move(body)); }

void Session::SetOption(const DownloadTo& download_to) { pimpl_->SetDownloadTarget(download_to); }
void Session::SetOption(DownloadTo&& download_to) { pimpl_->SetDownloadTarget(std::move(download_to)); }

template<class Handler>
void Session::SetOption(const on_response_<Handler>&& on_response) {pimpl_->SetHandler(std::move(on_response)); }

void Session::DELETE_()  { pimpl_->DELETE_(); }
void Session::GET()     { pimpl_->GET(); }
void Session::HEAD()    { pimpl_->HEAD(); }
void Session::OPTIONS() { pimpl_->OPTIONS(); }
void Session::PATCH()   { pimpl_->PATCH(); }
void Session::POST()    { pimpl_->POST(); }
void Session::PUT()     { pimpl_->PUT(); }

}


#endif 
