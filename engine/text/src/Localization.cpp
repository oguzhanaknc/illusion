#include <illusion/core/Assert.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/text/Localization.hpp>

#include <algorithm>
#include <string>

namespace illusion::text
{

namespace
{

LocaleId parseLocale(std::string_view tag) noexcept
{
    const std::optional<LocaleId> locale = LocaleId::parse(tag);
    ILL_ASSERT(locale.has_value(), "'{}' is not a valid locale", tag);
    return locale.value_or(LocaleId());
}

// True if every argument of @p translation also appears in @p english (both lists are sorted).
bool usesKnownArguments(const MessageFormat& translation, const MessageFormat& english) noexcept
{
    const std::span<const std::string> known = english.argumentNames();
    for (const std::string& name : translation.argumentNames())
    {
        if (!std::binary_search(known.begin(), known.end(), name))
        {
            return false;
        }
    }
    return true;
}

} // namespace

LocaleId pseudoLocale() noexcept
{
    return parseLocale("en-XA");
}

struct Localization::Impl
{
    struct Translation
    {
        std::string key;
        MessageFormat message;
    };

    struct Locale
    {
        LocaleFile file;                       // metadata; the entries are moved into `translations`
        std::vector<Translation> translations; // sorted by key
        bool superseded = false;               // replaced by a newer file; kept alive until the next resolve
    };

    struct Table
    {
        const StringTableData* data = nullptr;
        std::vector<MessageFormat> english;
        std::vector<MessageFormat> pseudo; // built when the pseudo-locale is first used
        std::vector<const MessageFormat*> active;
        std::vector<u8> fellBack;         // the active message is English although it should be translated
        mutable std::vector<u8> reported; // the fallback was logged
    };

    std::vector<Table> tables;
    // Heap-allocated so that the messages the tables point at stay where they are when files are added.
    std::vector<std::unique_ptr<Locale>> locales;
    LocaleId active = parseLocale("en");
    LocaleId requested = parseLocale("en");
    bool dirty = false;
    MessageContext context;
    std::vector<const Locale*> chain; // locale files in fallback order
    mutable usize missing = 0;
    mutable usize lastTable = 0;
    MessageFormat empty;

    const Locale* findLocale(const LocaleId& locale) const noexcept
    {
        for (const std::unique_ptr<Locale>& candidate : locales)
        {
            if (!candidate->superseded && candidate->file.locale == locale)
            {
                return candidate.get();
            }
        }
        return nullptr;
    }

    const Table* findTable(const StringTableData* data) const noexcept
    {
        if (lastTable < tables.size() && tables[lastTable].data == data)
        {
            return &tables[lastTable];
        }
        for (usize i = 0; i < tables.size(); ++i)
        {
            if (tables[i].data == data)
            {
                lastTable = i;
                return &tables[i];
            }
        }
        return nullptr;
    }

    bool isPseudo() const noexcept { return active == pseudoLocale(); }

    void buildChain()
    {
        chain.clear();
        std::vector<LocaleId> visited;
        const auto add = [&](const LocaleId& start) {
            for (std::optional<LocaleId> current = start; current; current = current->parent())
            {
                if (std::find(visited.begin(), visited.end(), *current) != visited.end())
                {
                    continue;
                }
                visited.push_back(*current);
                if (const Locale* locale = findLocale(*current))
                {
                    chain.push_back(locale);
                }
            }
        };
        if (!isPseudo())
        {
            add(active);
            for (usize i = 0; i < chain.size(); ++i)
            {
                if (chain[i]->file.fallback)
                {
                    add(*chain[i]->file.fallback);
                }
            }
        }

        context = MessageContext{};
        if (!chain.empty())
        {
            context.plural = chain.front()->file.plural;
            context.number = chain.front()->file.number;
        }
        else
        {
            context.plural = pluralRuleForLanguage(active.language()).value_or(PluralRule::English);
        }
    }

    static const MessageFormat* findTranslation(const Locale& locale, std::string_view key) noexcept
    {
        const auto it = std::lower_bound(locale.translations.begin(), locale.translations.end(), key,
                                         [](const Translation& t, std::string_view k) { return t.key < k; });
        return it != locale.translations.end() && it->key == key ? &it->message : nullptr;
    }

    void resolveTable(Table& table)
    {
        const usize count = table.data->keys.size();
        const bool pseudo = isPseudo();
        // English and its variants fall back to the compiled English text by design.
        const bool expectsTranslation = !pseudo && active.language() != "en";
        if (pseudo && table.pseudo.empty())
        {
            table.pseudo.reserve(count);
            for (const MessageFormat& english : table.english)
            {
                table.pseudo.push_back(english.pseudoLocalized());
            }
        }
        table.active.assign(count, nullptr);
        table.fellBack.assign(count, 0);
        table.reported.assign(count, 0);
        for (usize i = 0; i < count; ++i)
        {
            const std::string_view key = table.data->keys[i];
            for (const Locale* locale : chain)
            {
                const MessageFormat* translation = findTranslation(*locale, key);
                if (translation == nullptr)
                {
                    continue;
                }
                if (!usesKnownArguments(*translation, table.english[i]))
                {
                    ILL_LOG_WARN("Text",
                                 "the {} translation of \"{}\" uses arguments the English text does not have",
                                 locale->file.locale.tag(), key);
                    continue;
                }
                table.active[i] = translation;
                break;
            }
            if (table.active[i] == nullptr)
            {
                table.active[i] = pseudo ? &table.pseudo[i] : &table.english[i];
                table.fellBack[i] = static_cast<u8>(expectsTranslation ? 1 : 0);
            }
        }
    }

    void resolve()
    {
        ILL_PROFILE_SCOPE_N("Localization::resolve");
        buildChain();
        missing = 0;
        for (Table& table : tables)
        {
            resolveTable(table);
        }
        // No table points at replaced files any more.
        std::erase_if(locales, [](const std::unique_ptr<Locale>& locale) { return locale->superseded; });
        dirty = false;
    }
};

Localization::Localization()
    : m_impl(std::make_unique<Impl>())
{
}

Localization::~Localization() = default;

void Localization::addTable(const StringTableData& table)
{
    if (m_impl->findTable(&table) != nullptr)
    {
        return;
    }
    ILL_ASSERT(table.keys.size() == table.english.size(), "string table '{}' has {} keys but {} texts",
               table.name, table.keys.size(), table.english.size());
    Impl::Table state;
    state.data = &table;
    state.english.reserve(table.keys.size());
    for (usize i = 0; i < table.keys.size(); ++i)
    {
        auto compiled = MessageFormat::compile(i < table.english.size() ? table.english[i] : table.keys[i]);
        ILL_ASSERT(compiled.hasValue(), "English text of \"{}\" does not compile: {}", table.keys[i],
                   compiled.error().message);
        state.english.push_back(compiled.hasValue()
                                    ? std::move(compiled).value()
                                    : MessageFormat::compile(table.keys[i]).valueOr(MessageFormat()));
    }
    m_impl->tables.push_back(std::move(state));
    // Resolved against the current chain, so that the table is usable before the next frame boundary.
    m_impl->resolveTable(m_impl->tables.back());
}

Expected<void, LocaleError> Localization::addLocale(std::string_view json)
{
    auto file = LocaleFile::parse(json);
    if (!file)
    {
        return unexpected(std::move(file).error());
    }
    addLocale(std::move(file).value());
    return {};
}

void Localization::addLocale(LocaleFile file)
{
    auto locale = std::make_unique<Impl::Locale>();
    locale->translations.reserve(file.entries.size());
    for (LocaleEntry& entry : file.entries)
    {
        auto compiled = MessageFormat::compile(entry.text);
        if (!compiled)
        {
            ILL_LOG_WARN("Text", "the {} translation of \"{}\" does not compile (offset {}): {}",
                         file.locale.tag(), entry.key, compiled.error().offset, compiled.error().message);
            continue;
        }
        locale->translations.push_back(Impl::Translation{std::move(entry.key), std::move(compiled).value()});
    }
    file.entries.clear();
    std::sort(locale->translations.begin(), locale->translations.end(),
              [](const Impl::Translation& a, const Impl::Translation& b) { return a.key < b.key; });
    locale->file = std::move(file);

    // An older file of the same locale stays alive until the next resolve, because the active messages
    // may point into it until then.
    for (const std::unique_ptr<Impl::Locale>& existing : m_impl->locales)
    {
        if (existing->file.locale == locale->file.locale)
        {
            existing->superseded = true;
        }
    }
    m_impl->locales.push_back(std::move(locale));
    m_impl->dirty = true;
}

std::vector<LocaleId> Localization::availableLocales() const
{
    std::vector<LocaleId> locales{parseLocale("en"), pseudoLocale()};
    for (const std::unique_ptr<Impl::Locale>& locale : m_impl->locales)
    {
        if (std::find(locales.begin(), locales.end(), locale->file.locale) == locales.end())
        {
            locales.push_back(locale->file.locale);
        }
    }
    std::sort(locales.begin(), locales.end(),
              [](const LocaleId& a, const LocaleId& b) { return a.tag() < b.tag(); });
    return locales;
}

void Localization::setLocale(const LocaleId& locale)
{
    ILL_ASSERT(!locale.empty(), "setLocale() with an empty locale");
    m_impl->requested = locale;
}

void Localization::applyPendingLocale()
{
    if (m_impl->requested == m_impl->active && !m_impl->dirty)
    {
        return;
    }
    m_impl->active = m_impl->requested;
    m_impl->resolve();
}

const LocaleId& Localization::locale() const noexcept
{
    return m_impl->active;
}

const MessageContext& Localization::context() const noexcept
{
    return m_impl->context;
}

const MessageFormat& Localization::message(TextKey key) const
{
    const Impl::Table* table = key.table != nullptr ? m_impl->findTable(key.table) : nullptr;
    ILL_ASSERT(table != nullptr, "string table '{}' is not registered with addTable()",
               key.table != nullptr ? key.table->name : std::string_view("(null)"));
    if (table == nullptr || key.index >= table->active.size())
    {
        return m_impl->empty;
    }
    if (table->fellBack[key.index] != 0 && table->reported[key.index] == 0)
    {
        table->reported[key.index] = 1;
        ++m_impl->missing;
        ILL_LOG_DEBUG("Text", "\"{}\" has no {} translation; showing English", key.name(),
                      m_impl->active.tag());
    }
    return *table->active[key.index];
}

Expected<void, MessageError> Localization::format(TextKey key, std::span<const MessageArgument> arguments,
                                                  TextOutput& out) const
{
    return message(key).format(arguments, m_impl->context, out);
}

const MessageFormat* Localization::find(std::string_view name) const
{
    for (const Impl::Table& table : m_impl->tables)
    {
        const std::span<const std::string_view> keys = table.data->keys;
        const auto it = std::lower_bound(keys.begin(), keys.end(), name);
        if (it != keys.end() && *it == name)
        {
            return &message(TextKey{table.data, static_cast<u32>(it - keys.begin())});
        }
    }
    return nullptr;
}

usize Localization::missingKeyCount() const noexcept
{
    return m_impl->missing;
}

} // namespace illusion::text
