module mayquill:logger;
import maylog;

using namespace maylog;

constexpr maylog::config::LevelInfo Db = maylog::config::LevelInfo("myq debug", true, maylog::config::CatppuccinFrappe::teal());
constexpr maylog::config::LevelInfo If = maylog::config::LevelInfo("myq info", true, maylog::config::CatppuccinFrappe::flamingo(), 100);
constexpr maylog::config::LevelInfo Wn = maylog::config::LevelInfo("myq warn", true, maylog::config::CatppuccinFrappe::yellow(), 200);
constexpr maylog::config::LevelInfo Er = maylog::config::LevelInfo("myq error", true, maylog::config::CatppuccinFrappe::red(), 300, true);
