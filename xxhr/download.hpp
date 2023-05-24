#ifndef XXHR_DOWNLOAD_H
#define XXHR_DOWNLOAD_H

#include <initializer_list>
#include <memory>
#include <string>
#include <functional>
#include <optional>

#include "defines.hpp"

namespace xxhr {

/**
 * \brief Useful to download files to disk instead of having them in memory as a string
 * Especially when you're expecting a large binary 
 */
class DownloadTo {
  public:
    DownloadTo() = default;

    DownloadTo(
      const std::string &_destination_path,
      std::function<void(Response&&, const std::string&, const size_t)> &&_download_finished,
      std::optional<std::function<void(size_t, size_t)>> _on_progress = std::nullopt
    )
      : destination_path(_destination_path)
      , download_finished(std::move(_download_finished))
      , on_progress(std::move(_on_progress.value_or([](auto,auto) {}))) 
    { 
      // nothing to see here, carry on with your life
    }
    
    std::function<void(size_t, size_t)> on_progress;
    std::function<void(Response&&, const std::string&, size_t)> download_finished;
    std::string destination_path;
};

} // namespace xxhr

#endif
