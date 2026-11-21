/*
 * Copyright (c) 2026 NedcloarBR
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stratosphere.hpp>
#include <usbl/paths.hpp>
#include "usbl_i18n.hpp"
#include "../usbl_log.hpp"

extern "C" {
    extern const u8 en_US_json[];
    extern const u8 en_US_json_end[];
    extern const u8 pt_BR_json[];
    extern const u8 pt_BR_json_end[];
}

namespace ams::mitm::usbl::i18n {

    namespace {

        constexpr const char FallbackLocale[] = "en-US";
        constexpr size_t MaxSdLocaleFileSize = 16_KB;

        /* bin2s symbols: <file>_json and <file>_json_end. */
        struct EmbeddedLocale {
            const char *name;
            const u8 *begin;
            const u8 *end;
        };

        constexpr EmbeddedLocale EmbeddedLocales[] = {
            { "en-US", en_US_json, en_US_json_end },
            { "pt-BR", pt_BR_json, pt_BR_json_end },
        };

        constexpr size_t MaxEntries     = 64;
        constexpr size_t MaxKeyLength   = 96;
        constexpr size_t MaxValueLength = 256;

        struct Entry {
            char key[MaxKeyLength];
            char value[MaxValueLength];
        };

        struct Table {
            Entry entries[MaxEntries];
            size_t count;

            const char *Find(const char *key) const {
                for (size_t i = 0; i < count; ++i) {
                    if (std::strcmp(entries[i].key, key) == 0) {
                        return entries[i].value;
                    }
                }
                return nullptr;
            }
        };

        constinit Table g_fallback_table = {};
        constinit Table g_locale_table = {};
        /* setGetLanguageCode packs the code ("pt-BR", "en-US", "zh-Hans") into a u64. */
        constinit char g_locale[sizeof(u64) + 1] = {};
        constinit char g_sd_file_buffer[MaxSdLocaleFileSize];

        /* Accepts nested objects of strings, the subset the locale files use. */
        class Flattener {
            private:
                const char *m_cur;
                const char *m_end;
                Table *m_table;
                char m_path[MaxKeyLength];
            public:
                Flattener(const char *data, size_t size, Table *table) : m_cur(data), m_end(data + size), m_table(table), m_path() { /* ... */ }

                bool Parse() {
                    m_table->count = 0;
                    this->SkipWhitespace();
                    return this->ParseObject(0);
                }
            private:
                void SkipWhitespace() {
                    while (m_cur < m_end && (*m_cur == ' ' || *m_cur == '\t' || *m_cur == '\n' || *m_cur == '\r')) {
                        ++m_cur;
                    }
                }

                bool Consume(char c) {
                    this->SkipWhitespace();
                    if (m_cur < m_end && *m_cur == c) {
                        ++m_cur;
                        return true;
                    }
                    return false;
                }

                static size_t EncodeUtf8(char *dst, u32 cp) {
                    if (cp < 0x80)    { dst[0] = static_cast<char>(cp); return 1; }
                    if (cp < 0x800)   { dst[0] = static_cast<char>(0xC0 | (cp >> 6));  dst[1] = static_cast<char>(0x80 | (cp & 0x3F)); return 2; }
                    if (cp < 0x10000) { dst[0] = static_cast<char>(0xE0 | (cp >> 12)); dst[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); dst[2] = static_cast<char>(0x80 | (cp & 0x3F)); return 3; }
                    dst[0] = static_cast<char>(0xF0 | (cp >> 18)); dst[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); dst[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); dst[3] = static_cast<char>(0x80 | (cp & 0x3F));
                    return 4;
                }

                bool ParseHex4(u32 *out) {
                    if (m_end - m_cur < 4) {
                        return false;
                    }
                    u32 v = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char c = *m_cur++;
                        v <<= 4;
                        if      (c >= '0' && c <= '9') v |= c - '0';
                        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
                        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
                        else return false;
                    }
                    *out = v;
                    return true;
                }

                bool ParseString(char *dst, size_t dst_size) {
                    if (!this->Consume('"')) {
                        return false;
                    }

                    size_t len = 0;
                    auto append = [&](const char *src, size_t n) {
                        for (size_t i = 0; i < n && len + 1 < dst_size; ++i) {
                            dst[len++] = src[i];
                        }
                    };

                    while (m_cur < m_end && *m_cur != '"') {
                        char c = *m_cur++;
                        if (c != '\\') {
                            append(std::addressof(c), 1);
                            continue;
                        }
                        if (m_cur >= m_end) {
                            return false;
                        }

                        char esc = *m_cur++;
                        switch (esc) {
                            case 'n': esc = '\n'; break;
                            case 't': esc = '\t'; break;
                            case 'r': esc = '\r'; break;
                            case 'b': esc = '\b'; break;
                            case 'f': esc = '\f'; break;
                            case 'u': {
                                u32 cp;
                                if (!this->ParseHex4(std::addressof(cp))) {
                                    return false;
                                }
                                if (cp >= 0xD800 && cp < 0xDC00 && m_end - m_cur >= 6 && m_cur[0] == '\\' && m_cur[1] == 'u') {
                                    m_cur += 2;
                                    u32 low;
                                    if (!this->ParseHex4(std::addressof(low))) {
                                        return false;
                                    }
                                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                                }
                                char utf8[4];
                                append(utf8, EncodeUtf8(utf8, cp));
                                continue;
                            }
                            default: break;
                        }
                        append(std::addressof(esc), 1);
                    }

                    if (m_cur >= m_end) {
                        return false;
                    }
                    ++m_cur;
                    dst[len] = '\x00';
                    return true;
                }

                bool ParseObject(size_t path_len) {
                    if (!this->Consume('{')) {
                        return false;
                    }
                    if (this->Consume('}')) {
                        return true;
                    }

                    do {
                        char key[MaxKeyLength];
                        if (!this->ParseString(key, sizeof(key)) || !this->Consume(':')) {
                            return false;
                        }

                        const size_t key_len = util::SNPrintf(m_path + path_len, sizeof(m_path) - path_len, path_len > 0 ? ".%s" : "%s", key) + path_len;
                        if (key_len >= sizeof(m_path)) {
                            return false;
                        }

                        this->SkipWhitespace();
                        if (m_cur < m_end && *m_cur == '{') {
                            if (!this->ParseObject(key_len)) {
                                return false;
                            }
                        } else if (m_table->count < MaxEntries) {
                            Entry &entry = m_table->entries[m_table->count];
                            if (!this->ParseString(entry.value, sizeof(entry.value))) {
                                return false;
                            }
                            util::Strlcpy(entry.key, m_path, sizeof(entry.key));
                            ++m_table->count;
                        } else {
                            return false;
                        }

                        m_path[path_len] = '\x00';
                    } while (this->Consume(','));

                    return this->Consume('}');
                }
        };

        const EmbeddedLocale *FindEmbeddedLocale(const char *locale, bool match_language_only) {
            const size_t compare_len = match_language_only ? std::strcspn(locale, "-") : std::strlen(locale) + 1;
            for (const auto &embedded : EmbeddedLocales) {
                if (std::strncmp(embedded.name, locale, compare_len) == 0 && (!match_language_only || embedded.name[compare_len] == '-' || embedded.name[compare_len] == '\x00')) {
                    return std::addressof(embedded);
                }
            }
            return nullptr;
        }

        bool LoadEmbedded(Table *table, const EmbeddedLocale &embedded) {
            return Flattener(reinterpret_cast<const char *>(embedded.begin), embedded.end - embedded.begin, table).Parse();
        }

        bool LoadFromSdCard(Table *table, const char *locale) {
            char path[ams::fs::EntryNameLengthMax + 1];
            util::SNPrintf(path, sizeof(path), "%s:%s/%s.json", ams::fs::impl::SdCardFileSystemMountName, ::usbl::paths::I18nDir, locale);

            ams::fs::FileHandle file;
            if (R_FAILED(ams::fs::OpenFile(std::addressof(file), path, ams::fs::OpenMode_Read))) {
                return false;
            }
            ON_SCOPE_EXIT { ams::fs::CloseFile(file); };

            s64 size = 0;
            if (R_FAILED(ams::fs::GetFileSize(std::addressof(size), file)) || size <= 0 || size > static_cast<s64>(sizeof(g_sd_file_buffer))) {
                return false;
            }
            if (R_FAILED(ams::fs::ReadFile(file, 0, g_sd_file_buffer, size))) {
                return false;
            }

            return Flattener(g_sd_file_buffer, size, table).Parse();
        }

        void ReadSystemLocale() {
            u64 code = 0;
            if (R_FAILED(::setGetLanguageCode(std::addressof(code)))) {
                util::Strlcpy(g_locale, FallbackLocale, sizeof(g_locale));
                return;
            }

            std::memcpy(g_locale, std::addressof(code), sizeof(code));
            g_locale[sizeof(code)] = '\x00';
        }

    }

    void Initialize() {
        ReadSystemLocale();

        if (!LoadEmbedded(std::addressof(g_fallback_table), *FindEmbeddedLocale(FallbackLocale, false))) {
            Log("i18n: embedded %s failed to parse", FallbackLocale);
        }

        if (LoadFromSdCard(std::addressof(g_locale_table), g_locale)) {
            Log("i18n: locale %s (sd card, %zu strings)", g_locale, g_locale_table.count);
            return;
        }

        const EmbeddedLocale *embedded = FindEmbeddedLocale(g_locale, false);
        if (embedded == nullptr) {
            embedded = FindEmbeddedLocale(g_locale, true);
        }
        if (embedded != nullptr && LoadEmbedded(std::addressof(g_locale_table), *embedded)) {
            Log("i18n: locale %s (embedded %s, %zu strings)", g_locale, embedded->name, g_locale_table.count);
            return;
        }

        g_locale_table.count = 0;
        Log("i18n: locale %s not available, using %s", g_locale, FallbackLocale);
    }

    const char *GetLocale() {
        return g_locale;
    }

    void Format(char *dst, size_t dst_size, const char *key, std::initializer_list<Argument> args) {
        const char *src = g_locale_table.Find(key);
        if (src == nullptr) {
            src = g_fallback_table.Find(key);
        }
        if (src == nullptr) {
            src = key;
        }

        size_t len = 0;
        auto append = [&](const char *s, size_t n) {
            for (size_t i = 0; i < n && len + 1 < dst_size; ++i) {
                dst[len++] = s[i];
            }
        };

        while (*src != '\x00') {
            if (src[0] == '{' && src[1] == '{') {
                const char *close = std::strstr(src + 2, "}}");
                if (close != nullptr) {
                    const size_t name_len = close - (src + 2);
                    const Argument *match = nullptr;
                    for (const auto &arg : args) {
                        if (std::strlen(arg.name) == name_len && std::strncmp(arg.name, src + 2, name_len) == 0) {
                            match = std::addressof(arg);
                            break;
                        }
                    }
                    if (match != nullptr) {
                        append(match->value, std::strlen(match->value));
                        src = close + 2;
                        continue;
                    }
                }
            }
            append(src, 1);
            ++src;
        }

        if (dst_size > 0) {
            dst[len] = '\x00';
        }
    }

}
