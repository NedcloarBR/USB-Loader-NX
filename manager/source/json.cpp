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
#include <json.hpp>
#include <cstdio>
#include <switch.h>

namespace json {

    namespace {

        /* Nested objects of strings, flattened to "Section.Key"; anything else is a syntax error. */
        class Flattener {
            private:
                const std::string &text;
                size_t position = 0;
                Table &table;

                void SkipSpace() {
                    while (this->position < this->text.size() && (this->text[this->position] == ' ' || this->text[this->position] == '\t' || this->text[this->position] == '\n' || this->text[this->position] == '\r')) {
                        ++this->position;
                    }
                }

                bool Consume(char c) {
                    this->SkipSpace();
                    if (this->position < this->text.size() && this->text[this->position] == c) {
                        ++this->position;
                        return true;
                    }
                    return false;
                }

                bool ParseHex4(u32 *out) {
                    if (this->position + 4 > this->text.size()) {
                        return false;
                    }
                    *out = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char c = this->text[this->position++];
                        const int digit = c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1));
                        if (digit < 0) {
                            return false;
                        }
                        *out = *out * 16 + static_cast<u32>(digit);
                    }
                    return true;
                }

                static void AppendUtf8(std::string *out, u32 cp) {
                    if (cp < 0x80) {
                        out->push_back(static_cast<char>(cp));
                    } else if (cp < 0x800) {
                        out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
                        out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    } else if (cp < 0x10000) {
                        out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
                        out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                        out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    } else {
                        out->push_back(static_cast<char>(0xF0 | (cp >> 18)));
                        out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                        out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                        out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    }
                }

                bool ParseString(std::string *out) {
                    if (!this->Consume('"')) {
                        return false;
                    }
                    out->clear();
                    while (this->position < this->text.size()) {
                        const char c = this->text[this->position++];
                        if (c == '"') {
                            return true;
                        }
                        if (c != '\\') {
                            out->push_back(c);
                            continue;
                        }
                        if (this->position >= this->text.size()) {
                            return false;
                        }
                        switch (const char escaped = this->text[this->position++]; escaped) {
                            case 'n': out->push_back('\n'); break;
                            case 't': out->push_back('\t'); break;
                            case 'r': out->push_back('\r'); break;
                            case 'b': out->push_back('\b'); break;
                            case 'f': out->push_back('\f'); break;
                            case 'u': {
                                u32 cp;
                                if (!this->ParseHex4(&cp)) {
                                    return false;
                                }
                                /* A surrogate pair is two \u escapes. */
                                if (cp >= 0xD800 && cp < 0xDC00) {
                                    u32 low;
                                    if (!this->Consume('\\') || !this->Consume('u') || !this->ParseHex4(&low) || low < 0xDC00 || low >= 0xE000) {
                                        return false;
                                    }
                                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                                }
                                AppendUtf8(out, cp);
                                break;
                            }
                            default: out->push_back(escaped); break;
                        }
                    }
                    return false;
                }

                bool ParseObject(const std::string &prefix) {
                    if (!this->Consume('{')) {
                        return false;
                    }
                    if (this->Consume('}')) {
                        return true;
                    }
                    do {
                        std::string key;
                        if (!this->ParseString(&key) || !this->Consume(':')) {
                            return false;
                        }
                        const std::string path = prefix.empty() ? key : prefix + "." + key;
                        this->SkipSpace();
                        if (this->position < this->text.size() && this->text[this->position] == '{') {
                            if (!this->ParseObject(path)) {
                                return false;
                            }
                        } else {
                            std::string value;
                            if (!this->ParseString(&value)) {
                                return false;
                            }
                            this->table[path] = std::move(value);
                        }
                    } while (this->Consume(','));
                    return this->Consume('}');
                }

            public:
                Flattener(const std::string &text, Table &table) : text(text), table(table) {}

                bool Parse() {
                    return this->ParseObject({}) && (this->SkipSpace(), this->position == this->text.size());
                }
        };

        bool ReadFile(std::string *out, const std::string &path) {
            FILE *file = std::fopen(path.c_str(), "rb");
            if (file == nullptr) {
                return false;
            }
            std::fseek(file, 0, SEEK_END);
            const long size = std::ftell(file);
            std::fseek(file, 0, SEEK_SET);
            out->resize(size > 0 ? static_cast<size_t>(size) : 0);
            const bool ok = size > 0 && std::fread(out->data(), 1, out->size(), file) == out->size();
            std::fclose(file);
            return ok;
        }

    }

    bool ReadFlatFile(Table *out, const std::string &path) {
        std::string text;
        Table parsed;
        if (!ReadFile(&text, path) || !Flattener(text, parsed).Parse()) {
            return false;
        }
        for (auto &[key, value] : parsed) {
            (*out)[key] = std::move(value);
        }
        return true;
    }

}
