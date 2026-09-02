export module logger;
export import maylog;

using namespace maylog::config;
export using namespace maylog;

export constexpr LevelInfo Db = LevelInfo("myq debug", true, CatppuccinFrappe::teal());
export constexpr LevelInfo If = LevelInfo("myq info", true, CatppuccinFrappe::flamingo(), 100);
export constexpr LevelInfo Wn = LevelInfo("myq warn", true, CatppuccinFrappe::yellow(), 200);
export constexpr LevelInfo Er = LevelInfo("myq error", true, CatppuccinFrappe::red(), 300, true);
