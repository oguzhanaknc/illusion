# illusion::text — UTF-8, search folding, fuzzy matching, plural rules, MessageFormat and localization.

add_library(illusion_text STATIC
    src/FuzzyMatcher.cpp
    src/LocaleFile.cpp
    src/LocaleId.cpp
    src/Localization.cpp
    src/MessageFormat.cpp
    src/NumberFormat.cpp
    src/PluralRules.cpp
    src/SearchFold.cpp
    src/TextOutput.cpp
    src/Utf8.cpp)
add_library(illusion::text ALIAS illusion_text)

target_include_directories(illusion_text PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(illusion_text PUBLIC illusion::core)

illusion_configure_target(illusion_text)
