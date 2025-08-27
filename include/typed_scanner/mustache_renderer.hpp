#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace ts {

class MustacheRenderer {
public:
  struct Config {
    std::string template_dir = "templates";
    std::string partials_dir = "templates/partials";
    std::vector<std::string> static_js;
    std::vector<std::string> static_css;
  };

  MustacheRenderer();
  explicit MustacheRenderer(Config cfg);

  bool render_to_file(std::string_view template_name,
                      std::string_view context_json,
                      std::string_view out_path);

  bool render_to_dir(std::string_view template_name,
                     std::string_view context_json,
                     std::string_view out_dir,
                     std::string_view out_name,
                     bool copy_assets);

  const std::string& error() const { return err_; }
  const std::string& last_error() const noexcept { return err_; }

private:
  Config cfg_;
  std::string err_;
};

}
