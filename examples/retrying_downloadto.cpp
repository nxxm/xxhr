#include <xxhr/xxhr.hpp>
#include <boost/filesystem.hpp>

int main(int argc, const char** argv) {

  using namespace xxhr;
  namespace fs = boost::filesystem;

//! [GET-request-retry-vars]
  std::string url = (argc < 2) ? "http://httpbin.org/post" : argv[1];
  std::function<void(void)> run_download;
  auto retries_count = 3;
  auto retry_on_fail = [&](xxhr::Response &&resp, const std::string &output_path, const size_t file_size) {
    std::cout << "ERROR, retrying, download of url : " << url << ", err: " << resp.error << std::endl;
    if ( (resp.error || (resp.status_code != 200) ) && (retries_count > 0) ) {
      --retries_count;
      run_download();
    } else {
      std::cout << "Final outcome : " << resp.status_code << std::endl;
      // correct response
    }
  };

//! [GET-request-retry-vars]


//! [GET-request-retry]
  auto progressMeter = [](size_t dltotal, size_t dlnow) {
    std::cout << "[PROGRESS] " << dlnow << " of " << dltotal << "\n";
  };

  run_download = [&]() { 

    std::string destination = fs::path(url).filename().generic_string();
    std::cout << "run_download : " << url << ", destination: " << destination << std::endl;
    GET( 
      url,
      DownloadTo(destination, retry_on_fail, progressMeter)
    );

  };
//! [GET-request-retry]





//! [GET-request-retry-do]
  run_download();
//! [GET-request-retry-do]

  return 0;
}

/*! \page retrying-downloadto-cpp Retry on failure 

  It's always good to give some more chances to the same action, this can easily be achieved by wrapping the request calls :

  ## on_response handler

  We can use the indirection offered by `std::function`, so that the request and the on_response handler can respectively refers each other.

  \snippet this GET-request-retry-vars

  ## define the request 

  \snippet this GET-request-retry

  ## on_response definition
  Now we can write an on_response handler which will calls the request again if it failed and there are still `retries_count` available.

  \snippet this GET-request-retry-onfail

  ## Don't forget to initiate the first try 

  \snippet this GET-request-retry-do

*/
