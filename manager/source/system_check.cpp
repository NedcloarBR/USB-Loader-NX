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
#include <system_check.hpp>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <vector>
#include <sd_files.hpp>

namespace system_check {

    namespace {

        /* Atmosphère's own spl config items. */
        constexpr u32 ExosphereApiVersion    = 65000;
        constexpr u32 ExosphereGitCommitHash = 65003;

        constexpr const char ConfigDirectory[]    = "sdmc:/atmosphere/config";
        constexpr const char SystemSettingsName[] = "system_settings.ini";
        constexpr const char Section[]            = "usbloadernx";
        constexpr const char EnableLine[]         = "enable = u8!0x1";

        void ReadAtmosphereVersion(Report *report) {
            if (R_FAILED(splInitialize())) {
                return;
            }
            u64 version = 0, commit = 0;
            if (R_SUCCEEDED(splGetConfig(static_cast<SplConfigItem>(ExosphereApiVersion), &version))) {
                char text[16];
                std::snprintf(text, sizeof(text), "%u.%u.%u", static_cast<u32>((version >> 56) & 0xFF), static_cast<u32>((version >> 48) & 0xFF), static_cast<u32>((version >> 40) & 0xFF));
                report->atmosphere_version = text;
            }
            if (R_SUCCEEDED(splGetConfig(static_cast<SplConfigItem>(ExosphereGitCommitHash), &commit))) {
                char text[24];
                std::snprintf(text, sizeof(text), "%016" PRIx64, commit);
                report->atmosphere_commit = std::string(text, 9);
            }
            splExit();
        }

        /* -1 when the setting does not exist (Atmosphère without USB Loader NX registers no default for it). */
        int ReadEnableSetting() {
            if (R_FAILED(setsysInitialize())) {
                return -1;
            }
            u8 value = 0;
            u64 size = 0;
            const Result rc = setsysGetSettingsItemValue(Section, "enable", &value, sizeof(value), &size);
            setsysExit();
            return R_SUCCEEDED(rc) && size == sizeof(value) ? value : -1;
        }

        std::vector<u8> ReadWholeFile(const std::string &path) {
            std::vector<u8> data;
            if (FILE *file = std::fopen(path.c_str(), "rb"); file != nullptr) {
                std::fseek(file, 0, SEEK_END);
                const long size = std::ftell(file);
                std::fseek(file, 0, SEEK_SET);
                if (size > 0 && size < 1024 * 1024) {
                    data.resize(static_cast<size_t>(size));
                    data.resize(std::fread(data.data(), 1, data.size(), file));
                }
                std::fclose(file);
            }
            return data;
        }

        /* Packs like CNX copy the boot option's file (cfw_system_settings.ini, stock_system_settings.ini…) over
         * system_settings.ini at every boot, so editing system_settings.ini itself would be undone. The file with the very
         * same content is the one the boot option in use came from. */
        std::string FindSettingsFile() {
            const std::string system_settings = std::string(ConfigDirectory) + "/" + SystemSettingsName;
            const std::vector<u8> current = ReadWholeFile(system_settings);
            if (!current.empty()) {
                if (DIR *dir = opendir(ConfigDirectory); dir != nullptr) {
                    std::string match;
                    while (const dirent *entry = readdir(dir)) {
                        const std::string name = entry->d_name;
                        const bool candidate = name != SystemSettingsName && name.find("system_settings") != std::string::npos && name.size() > 4 && name.compare(name.size() - 4, 4, ".ini") == 0;
                        if (candidate && ReadWholeFile(std::string(ConfigDirectory) + "/" + name) == current) {
                            match = name;
                            break;
                        }
                    }
                    closedir(dir);
                    if (!match.empty()) {
                        return std::string("atmosphere/config/") + match;
                    }
                }
            }
            return std::string("atmosphere/config/") + SystemSettingsName;
        }

        std::string Trim(const std::string &text) {
            const size_t first = text.find_first_not_of(" \t\r\n");
            const size_t last = text.find_last_not_of(" \t\r\n");
            return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
        }

        std::vector<std::string> ReadLines(const std::string &path) {
            std::vector<std::string> lines;
            const std::vector<u8> data = ReadWholeFile(path);
            std::string line;
            for (const u8 c : data) {
                if (c == '\n') {
                    lines.push_back(line);
                    line.clear();
                } else if (c != '\r') {
                    line.push_back(static_cast<char>(c));
                }
            }
            if (!line.empty()) {
                lines.push_back(line);
            }
            return lines;
        }

        bool IsEnableKey(const std::string &line) {
            const std::string text = Trim(line);
            const size_t equals = text.find('=');
            return equals != std::string::npos && Trim(text.substr(0, equals)) == "enable";
        }

        bool FileEnables(const std::string &path) {
            bool in_section = false;
            for (const auto &line : ReadLines(path)) {
                const std::string text = Trim(line);
                if (!text.empty() && text[0] == '[') {
                    in_section = Trim(text.substr(1, text.find(']') - 1)) == Section;
                } else if (in_section && IsEnableKey(text)) {
                    const std::string value = Trim(text.substr(text.find('=') + 1));
                    return value == "u8!0x1" || value == "u8!1";
                }
            }
            return false;
        }

        Result WriteEnabled(const std::string &path) {
            std::vector<std::string> lines = ReadLines(path);

            /* The key in [usbloadernx] is replaced; else it goes right under the section's header, or in a new section. */
            bool in_section = false, done = false;
            size_t header = SIZE_MAX;
            for (size_t i = 0; i < lines.size() && !done; ++i) {
                const std::string text = Trim(lines[i]);
                if (!text.empty() && text[0] == '[') {
                    in_section = Trim(text.substr(1, text.find(']') - 1)) == Section;
                    if (in_section) {
                        header = i;
                    }
                } else if (in_section && IsEnableKey(text)) {
                    lines[i] = EnableLine;
                    done = true;
                }
            }
            if (!done && header != SIZE_MAX) {
                lines.insert(lines.begin() + static_cast<long>(header) + 1, EnableLine);
            } else if (!done) {
                lines.push_back(std::string("[") + Section + "]");
                lines.push_back(EnableLine);
            }

            if (sd::Exists(path)) {
                const std::vector<u8> original = ReadWholeFile(path);
                if (FILE *backup = std::fopen((path + ".bak").c_str(), "wb"); backup != nullptr) {
                    std::fwrite(original.data(), 1, original.size(), backup);
                    std::fclose(backup);
                }
            }

            FILE *file = std::fopen(path.c_str(), "w");
            if (file == nullptr) {
                return MAKERESULT(Module_Libnx, LibnxError_IoError);
            }
            for (const auto &line : lines) {
                std::fprintf(file, "%s\n", line.c_str());
            }
            const bool ok = std::fflush(file) == 0;
            std::fclose(file);
            return ok ? 0 : MAKERESULT(Module_Libnx, LibnxError_IoError);
        }

    }

    Report Run() {
        Report report;
        ReadAtmosphereVersion(&report);

        if (R_SUCCEEDED(usbl::Initialize())) {
            report.has_version = R_SUCCEEDED(usbl::GetVersion(&report.version));
            report.state = report.has_version && report.version.major == usbl::protocol::VersionMajor ? State::Ready : State::VersionMismatch;
            if (report.state != State::Ready) {
                usbl::Exit();
            }
            return report;
        }

        switch (ReadEnableSetting()) {
            case -1:
                report.state = State::NotOurAtmosphere;
                break;
            case 0:
                report.state = State::Disabled;
                report.settings_file = FindSettingsFile();
                report.file_enables = FileEnables("sdmc:/" + report.settings_file);
                break;
            default:
                report.state = State::NotStarted;
                break;
        }
        return report;
    }

    Result EnableUsbLoaderNx(const std::string &settings_file) {
        const std::string system_settings = std::string("atmosphere/config/") + SystemSettingsName;
        if (const Result rc = WriteEnabled("sdmc:/" + settings_file); R_FAILED(rc)) {
            return rc;
        }
        /* Also the copy Atmosphère reads, in case the boot option's file was guessed wrong. */
        if (settings_file != system_settings && sd::Exists("sdmc:/" + system_settings)) {
            return WriteEnabled("sdmc:/" + system_settings);
        }
        return 0;
    }

    void Reboot() {
        if (R_SUCCEEDED(bpcInitialize())) {
            bpcRebootSystem();
            bpcExit();
        }
    }

}
