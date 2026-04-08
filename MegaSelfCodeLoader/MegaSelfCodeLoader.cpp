#include <windows.h>
#include <wininet.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <atomic>
#include <wrl.h>
#include <wil/com.h>
#include "WebView2.h"
#include <nlohmann/json.hpp>
#include <map>
#include <set>
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")

using namespace Microsoft::WRL;
namespace fs = std::filesystem;
using json = nlohmann::json;

#define WM_WEBVIEW_UPDATE (WM_USER + 101)


std::wstring CHEAT_NAME = L"Example";
const std::wstring MC_VERSION = L"1.21.11";
const std::wstring FABRIC_PROFILE_FALLBACK = L"Fabric " + MC_VERSION;
const std::wstring FABRIC_LOADER_FALLBACK = L"0.18.6";
const std::wstring FABRIC_INSTALLER_URL_FALLBACK = L"https://maven.fabricmc.net/net/fabricmc/fabric-installer/1.1.1/fabric-installer-1.1.1.jar";
const std::wstring MOJANG_VERSION_MANIFEST_URL = L"https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";
const std::wstring MOD_FILE_NAME = L"modmenu-17.0.0.jar";
const std::wstring FABRIC_API_FILE_NAME = L"fabric-api-0.141.3+1.21.11.jar";
const std::wstring JRE_URL = L"https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.10%2B7/OpenJDK21U-jre_x64_windows_hotspot_21.0.10_7.zip"; // Это JRE если надо замени
const std::wstring MOD_URL = L"https://cdn.modrinth.com/data/mOgUt4GM/versions/Tyk71iSw/modmenu-17.0.0.jar"; // Это твой главный мод клиента. Fabric 1.21.11
const std::wstring ADD_MOD_URL = L"https://cdn.modrinth.com/data/P7dR8mSH/versions/i5tSkVBH/fabric-api-0.141.3%2B1.21.11.jar"; // Это Fabric API

HWND g_hWnd = nullptr;
wil::com_ptr<ICoreWebView2Controller> g_webviewController;
wil::com_ptr<ICoreWebView2> g_webview;
int g_RamAmount = 4028;
std::atomic<DWORD> g_GamePID{ 0 };
std::atomic<bool> g_CancelDownload{ false };
std::atomic<bool> g_InstallInProgress{ false };

bool g_DarkTheme = true;
bool g_LangRu = true;
bool g_HasSavedPrefs = false;
std::wstring g_Nickname = L"Player";
bool g_ExtraPanelOpen = false;

const int MAIN_WIDTH = 382;
const int MAIN_HEIGHT = 532;
const int EXTRA_WIDTH = 220;

struct RegState { bool isInstalled; int ram; bool darkTheme; bool langRu; bool hasPrefs; std::wstring nickname; };

static const BYTE AES_KEY[32] = {
    0x4F,0x2B,0x7E,0x15,0x16,0xA8,0x09,0xCF,0xAA,0xDF,0x2C,0x9B,0x7D,0x51,0x3A,0xE8,
    0xC1,0x03,0x5E,0xF7,0xD4,0x62,0xB9,0x80,0x1C,0xA6,0x3F,0x58,0xE9,0x74,0x0D,0xBB
};
static const BYTE AES_IV[16] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F
};

std::wstring GetBaseDir() { return L"C:\\" + CHEAT_NAME + L"\\"; }
std::wstring GetMinecraftDir() { return GetBaseDir() + L".minecraft\\"; }
std::wstring GetModsDir() { return GetMinecraftDir() + L"mods\\"; }
std::wstring GetVersionRootDir() { return GetMinecraftDir() + L"versions\\"; }

std::vector<int> ParseVersionTokens(const std::wstring& value) {
    std::vector<int> tokens;
    std::wstring current;
    for (wchar_t c : value) {
        if (c >= L'0' && c <= L'9') {
            current += c;
        }
        else if (!current.empty()) {
            try { tokens.push_back(std::stoi(current)); }
            catch (...) { tokens.push_back(0); }
            current.clear();
        }
    }
    if (!current.empty()) {
        try { tokens.push_back(std::stoi(current)); }
        catch (...) { tokens.push_back(0); }
    }
    while (tokens.size() < 4) tokens.push_back(0);
    return tokens;
}

bool IsVersionGreater(const std::wstring& left, const std::wstring& right) {
    auto lv = ParseVersionTokens(left);
    auto rv = ParseVersionTokens(right);
    size_t count = lv.size() < rv.size() ? lv.size() : rv.size();
    for (size_t i = 0; i < count; i++) {
        if (lv[i] > rv[i]) return true;
        if (lv[i] < rv[i]) return false;
    }
    return false;
}

std::wstring FindInstalledFabricProfileName() {
    std::wstring versionsDir = GetVersionRootDir();
    if (!fs::exists(versionsDir)) return L"";

    const std::wstring prefix = L"fabric-loader-";
    const std::wstring suffix = L"-" + MC_VERSION;
    std::wstring bestProfile;
    std::wstring bestLoaderVer;

    for (auto const& entry : fs::directory_iterator(versionsDir)) {
        if (!entry.is_directory()) continue;

        std::wstring folderName = entry.path().filename().wstring();
        if (folderName.rfind(prefix, 0) != 0) continue;
        if (folderName.length() <= prefix.length() + suffix.length()) continue;
        if (folderName.substr(folderName.length() - suffix.length()) != suffix) continue;

        std::wstring loaderVersion = folderName.substr(prefix.length(), folderName.length() - prefix.length() - suffix.length());
        std::wstring profileJson = entry.path().wstring() + L"\\" + folderName + L".json";
        if (!fs::exists(profileJson)) continue;

        if (bestProfile.empty() || IsVersionGreater(loaderVersion, bestLoaderVer)) {
            bestProfile = folderName;
            bestLoaderVer = loaderVersion;
        }
    }

    return bestProfile;
}

std::wstring GetActiveFabricProfileName() {
    std::wstring detected = FindInstalledFabricProfileName();
    if (!detected.empty()) return detected;
    return FABRIC_PROFILE_FALLBACK;
}

std::wstring GetVersionDir() { return GetVersionRootDir() + GetActiveFabricProfileName() + L"\\"; }
std::wstring GetVersionJsonPath() { std::wstring profile = GetActiveFabricProfileName(); return GetVersionRootDir() + profile + L"\\" + profile + L".json"; }
std::wstring GetVersionJarPath() { std::wstring profile = GetActiveFabricProfileName(); return GetVersionRootDir() + profile + L"\\" + profile + L".jar"; }
std::wstring GetVanillaVersionDir() { return GetVersionRootDir() + MC_VERSION + L"\\"; }
std::wstring GetVanillaVersionJsonPath() { return GetVanillaVersionDir() + MC_VERSION + L".json"; }
std::wstring GetVanillaVersionJarPath() { return GetVanillaVersionDir() + MC_VERSION + L".jar"; }

bool AESEncrypt(const std::vector<BYTE>& plaintext, std::vector<BYTE>& ciphertext) {
    BCRYPT_ALG_HANDLE hAlg = NULL; BCRYPT_KEY_HANDLE hKey = NULL; NTSTATUS status;
    status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0);
    if (!BCRYPT_SUCCESS(status)) return false;
    status = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PBYTE)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
    if (!BCRYPT_SUCCESS(status)) { BCryptCloseAlgorithmProvider(hAlg, 0); return false; }
    status = BCryptGenerateSymmetricKey(hAlg, &hKey, NULL, 0, (PBYTE)AES_KEY, sizeof(AES_KEY), 0);
    if (!BCRYPT_SUCCESS(status)) { BCryptCloseAlgorithmProvider(hAlg, 0); return false; }
    DWORD cb = 0; BYTE iv[16]; memcpy(iv, AES_IV, 16);
    status = BCryptEncrypt(hKey, (PBYTE)plaintext.data(), (ULONG)plaintext.size(), NULL, iv, 16, NULL, 0, &cb, BCRYPT_BLOCK_PADDING);
    if (!BCRYPT_SUCCESS(status)) { BCryptDestroyKey(hKey); BCryptCloseAlgorithmProvider(hAlg, 0); return false; }
    ciphertext.resize(cb); memcpy(iv, AES_IV, 16);
    status = BCryptEncrypt(hKey, (PBYTE)plaintext.data(), (ULONG)plaintext.size(), NULL, iv, 16, ciphertext.data(), cb, &cb, BCRYPT_BLOCK_PADDING);
    if (!BCRYPT_SUCCESS(status)) { BCryptDestroyKey(hKey); BCryptCloseAlgorithmProvider(hAlg, 0); return false; }
    ciphertext.resize(cb); BCryptDestroyKey(hKey); BCryptCloseAlgorithmProvider(hAlg, 0); return true;
}

bool AESDecrypt(const std::vector<BYTE>& ciphertext, std::vector<BYTE>& plaintext) {
    BCRYPT_ALG_HANDLE hAlg = NULL; BCRYPT_KEY_HANDLE hKey = NULL; NTSTATUS status;
    status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0);
    if (!BCRYPT_SUCCESS(status)) return false;
    status = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PBYTE)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
    if (!BCRYPT_SUCCESS(status)) { BCryptCloseAlgorithmProvider(hAlg, 0); return false; }
    status = BCryptGenerateSymmetricKey(hAlg, &hKey, NULL, 0, (PBYTE)AES_KEY, sizeof(AES_KEY), 0);
    if (!BCRYPT_SUCCESS(status)) { BCryptCloseAlgorithmProvider(hAlg, 0); return false; }
    DWORD cb = 0; BYTE iv[16]; memcpy(iv, AES_IV, 16);
    status = BCryptDecrypt(hKey, (PBYTE)ciphertext.data(), (ULONG)ciphertext.size(), NULL, iv, 16, NULL, 0, &cb, BCRYPT_BLOCK_PADDING);
    if (!BCRYPT_SUCCESS(status)) { BCryptDestroyKey(hKey); BCryptCloseAlgorithmProvider(hAlg, 0); return false; }
    plaintext.resize(cb); memcpy(iv, AES_IV, 16);
    status = BCryptDecrypt(hKey, (PBYTE)ciphertext.data(), (ULONG)ciphertext.size(), NULL, iv, 16, plaintext.data(), cb, &cb, BCRYPT_BLOCK_PADDING);
    if (!BCRYPT_SUCCESS(status)) { BCryptDestroyKey(hKey); BCryptCloseAlgorithmProvider(hAlg, 0); return false; }
    plaintext.resize(cb); BCryptDestroyKey(hKey); BCryptCloseAlgorithmProvider(hAlg, 0); return true;
}

bool ProtectDataForCurrentUser(const std::vector<BYTE>& plaintext, std::vector<BYTE>& ciphertext) {
    DATA_BLOB in = {};
    DATA_BLOB out = {};
    in.pbData = const_cast<BYTE*>(plaintext.data());
    in.cbData = static_cast<DWORD>(plaintext.size());
    if (!CryptProtectData(&in, L"LauncherPrefs", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return false;
    ciphertext.assign(out.pbData, out.pbData + out.cbData);
    LocalFree(out.pbData);
    return true;
}

bool UnprotectDataForCurrentUser(const std::vector<BYTE>& ciphertext, std::vector<BYTE>& plaintext) {
    DATA_BLOB in = {};
    DATA_BLOB out = {};
    in.pbData = const_cast<BYTE*>(ciphertext.data());
    in.cbData = static_cast<DWORD>(ciphertext.size());
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return false;
    plaintext.assign(out.pbData, out.pbData + out.cbData);
    LocalFree(out.pbData);
    return true;
}

void SafePostJson(const std::wstring& j) {
    if (!g_hWnd) return;
    std::wstring* m = new std::wstring(j);
    if (!PostMessage(g_hWnd, WM_WEBVIEW_UPDATE, 0, (LPARAM)m)) delete m;
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return ""; int s = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), 0, 0, 0, 0);
    std::string r(s, 0); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &r[0], s, 0, 0); return r;
}

std::wstring Utf8ToWide(const std::string& u) {
    if (u.empty()) return L""; int s = MultiByteToWideChar(CP_UTF8, 0, u.c_str(), (int)u.size(), 0, 0);
    std::wstring r(s, 0); MultiByteToWideChar(CP_UTF8, 0, u.c_str(), (int)u.size(), &r[0], s); return r;
}

std::wstring EscapeJsonString(const std::wstring& input) {
    std::wstringstream escaped;
    for (wchar_t ch : input) {
        switch (ch) {
        case L'"': escaped << L"\\\""; break;
        case L'\\': escaped << L"\\\\"; break;
        case L'\b': escaped << L"\\b"; break;
        case L'\f': escaped << L"\\f"; break;
        case L'\n': escaped << L"\\n"; break;
        case L'\r': escaped << L"\\r"; break;
        case L'\t': escaped << L"\\t"; break;
        default:
            if (ch >= 0 && ch < 0x20) {
                escaped << L"\\u"
                    << std::hex << std::uppercase << std::setw(4) << std::setfill(L'0') << static_cast<int>(ch)
                    << std::nouppercase << std::dec;
            }
            else {
                escaped << ch;
            }
            break;
        }
    }
    return escaped.str();
}

std::string SanitizeNicknameForJson(const std::wstring& nick) {
    std::string u = WideToUtf8(nick); std::string result;
    for (char c : u) {
        if (c == '"') result += "\\\""; else if (c == '\\') result += "\\\\";
        else if (c == '\n' || c == '\r') continue; else result += c;
    }
    return result;
}

std::string SanitizeNicknameForStorage(const std::wstring& nick) {
    std::string u = WideToUtf8(nick); std::string result;
    for (char c : u) { if (c == ';' || c == '=' || c == '\n' || c == '\r') continue; result += c; }
    if (result.empty()) result = "Player"; if (result.length() > 16) result = result.substr(0, 16);
    return result;
}

void SaveRegistry(bool installed, int ram, bool darkTheme, bool langRu, const std::wstring& nickname) {
    HKEY hKey; std::wstring regPath = L"SOFTWARE\\" + CHEAT_NAME;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, regPath.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        std::string nickStr = SanitizeNicknameForStorage(nickname);
        std::string p;
        p += "RAM=" + std::to_string(ram) + ";";
        p += "STATUS=" + std::string(installed ? "INSTALLED_OK" : "NOT_INSTALLED") + ";";
        p += "THEME=" + std::string(darkTheme ? "DARK" : "LIGHT") + ";";
        p += "LANG=" + std::string(langRu ? "RU" : "EN") + ";";
        p += "NICK=" + nickStr + ";";
        p += "PREFS=YES;";
        p += "CHECKSUM=" + std::to_string(ram * 31337 + (installed ? 99991 : 13579)) + ";";
        std::vector<BYTE> pv(p.begin(), p.end()); std::vector<BYTE> ev;
        bool encrypted = ProtectDataForCurrentUser(pv, ev) || AESEncrypt(pv, ev);
        if (encrypted) RegSetValueExW(hKey, L"Data", 0, REG_BINARY, ev.data(), (DWORD)ev.size());
        RegCloseKey(hKey);
    }
}

RegState LoadRegistry() {
    RegState state = { false, 4028, true, true, false, L"Player" };
    HKEY hKey; std::wstring regPath = L"SOFTWARE\\" + CHEAT_NAME;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, regPath.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD dataSize = 0;
        if (RegQueryValueExW(hKey, L"Data", NULL, NULL, NULL, &dataSize) == ERROR_SUCCESS && dataSize > 0) {
            std::vector<BYTE> ev(dataSize);
            if (RegQueryValueExW(hKey, L"Data", NULL, NULL, ev.data(), &dataSize) == ERROR_SUCCESS) {
                std::vector<BYTE> pv;
                if (UnprotectDataForCurrentUser(ev, pv) || AESDecrypt(ev, pv)) {
                    std::string pd(pv.begin(), pv.end());
                    auto ex = [&](const std::string& k) -> std::string {
                        size_t p2 = pd.find(k + "="); if (p2 == std::string::npos) return "";
                        p2 += k.length() + 1; size_t e = pd.find(";", p2); if (e == std::string::npos) return "";
                        return pd.substr(p2, e - p2);
                        };
                    std::string rs = ex("RAM"), ss = ex("STATUS"), ts = ex("THEME"), ls = ex("LANG"), ns = ex("NICK"), ps = ex("PREFS"), cs = ex("CHECKSUM");
                    if (!rs.empty()) {
                        try {
                            int rv = std::stoi(rs); bool iv = (ss == "INSTALLED_OK");
                            int ec = rv * 31337 + (iv ? 99991 : 13579);
                            if (!cs.empty() && std::stoi(cs) == ec) { state.ram = rv; state.isInstalled = iv; }
                        }
                        catch (...) {}
                    }
                    state.darkTheme = (ts != "LIGHT"); state.langRu = (ls != "EN"); state.hasPrefs = (ps == "YES");
                    if (!ns.empty()) state.nickname = Utf8ToWide(ns);
                }
            }
        }
        RegCloseKey(hKey);
    }
    return state;
}

void SendProgress(double percent, double currentMb, double totalMb, const std::wstring& status) {
    std::wstringstream js; js.imbue(std::locale::classic());
    std::wstring safeStatus = EscapeJsonString(status);
    js << L"{ \"type\": \"progress\", \"percent\": " << percent
        << L", \"current\": \"" << std::fixed << std::setprecision(1) << currentMb << L"MB\""
        << L", \"total\": \"" << std::fixed << std::setprecision(1) << totalMb << L"MB\""
        << L", \"status\": \"" << safeStatus << L"\" }";
    SafePostJson(js.str());
}

void SendError(const std::wstring& errorMsg) {
    std::wstring safeMessage = EscapeJsonString(errorMsg);
    std::wstringstream js; js << L"{ \"type\": \"error\", \"message\": \"" << safeMessage << L"\" }"; SafePostJson(js.str());
}

bool DownloadFile(const std::wstring& url, const std::wstring& destPath, const std::wstring& statusText) {
    HINTERNET hI = InternetOpenW(L"Launcher/1.0", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hI) { SendError(L"InternetOpen failed"); return false; }

    DWORD openFlags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_KEEP_CONNECTION;
    if (url.rfind(L"https://", 0) == 0) openFlags |= INTERNET_FLAG_SECURE;

    HINTERNET hU = InternetOpenUrlW(hI, url.c_str(), NULL, 0, openFlags, 0);
    if (!hU) { DWORD e = GetLastError(); SendError(L"InternetOpenUrl failed: " + std::to_wstring(e)); InternetCloseHandle(hI); return false; }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    if (HttpQueryInfoW(hU, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusCodeSize, NULL)) {
        if (statusCode < 200 || statusCode >= 300) {
            SendError(L"HTTP error " + std::to_wstring(statusCode) + L" for URL: " + url);
            InternetCloseHandle(hU);
            InternetCloseHandle(hI);
            return false;
        }
    }

    DWORD ts = 0, ls = sizeof(ts); HttpQueryInfoW(hU, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &ts, &ls, NULL);
    fs::create_directories(fs::path(destPath).parent_path());
    std::ofstream of(destPath, std::ios::binary);
    if (!of) { SendError(L"Cannot create: " + destPath); InternetCloseHandle(hU); InternetCloseHandle(hI); return false; }

    char buf[8192];
    DWORD tr = 0;
    double tm = ts / 1024.0 / 1024.0;
    bool readOk = true;

    while (true) {
        DWORD br = 0;
        if (!InternetReadFile(hU, buf, sizeof(buf), &br)) {
            DWORD e = GetLastError();
            SendError(L"InternetReadFile failed: " + std::to_wstring(e));
            readOk = false;
            break;
        }
        if (br == 0) break;
        if (g_CancelDownload) {
            of.close();
            InternetCloseHandle(hU);
            InternetCloseHandle(hI);
            fs::remove(destPath);
            return false;
        }

        of.write(buf, br);
        if (!of) {
            SendError(L"Failed to write file: " + destPath);
            readOk = false;
            break;
        }

        tr += br;
        double cm = tr / 1024.0 / 1024.0, pc = (ts > 0) ? (double)tr / ts * 100.0 : 0.0;
        SendProgress(pc, cm, tm, statusText);
    }

    of.close();
    InternetCloseHandle(hU);
    InternetCloseHandle(hI);

    if (!readOk) {
        fs::remove(destPath);
        return false;
    }

    if (ts > 0 && tr != ts) {
        SendError(L"Downloaded size mismatch for: " + destPath);
        fs::remove(destPath);
        return false;
    }

    return true;
}

bool UnzipWithPowerShell(const std::wstring& zipPath, const std::wstring& targetDir) {
    if (g_CancelDownload) return false;
    fs::create_directories(targetDir);
    std::wstring cmd = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"Expand-Archive -LiteralPath '" + zipPath + L"' -DestinationPath '" + targetDir + L"' -Force\"";
    STARTUPINFOW si = { sizeof(si) }; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi; std::vector<wchar_t> cb(cmd.begin(), cmd.end()); cb.push_back(0);
    if (CreateProcessW(NULL, cb.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE); DWORD ec = 0; GetExitCodeProcess(pi.hProcess, &ec);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        if (ec != 0) { SendError(L"Unzip failed, code: " + std::to_wstring(ec)); return false; }
        return true;
    }
    SendError(L"Failed to start PowerShell"); return false;
}

std::wstring FindJavaExe(bool useJavaw) {
    std::wstring base = GetBaseDir();
    std::wstring exeName = useJavaw ? L"javaw.exe" : L"java.exe";
    std::wstring je = base + L"jre\\bin\\" + exeName;
    if (fs::exists(je)) return je;
    if (fs::exists(base + L"jre")) {
        for (auto const& d : fs::directory_iterator(base + L"jre")) {
            if (d.is_directory()) {
                std::wstring sp = d.path().wstring() + L"\\bin\\" + exeName;
                if (fs::exists(sp)) return sp;
            }
        }
    }
    je = base + L"jre\\bin\\" + (useJavaw ? L"java.exe" : L"javaw.exe");
    if (fs::exists(je)) return je;
    if (fs::exists(base + L"jre")) {
        for (auto const& d : fs::directory_iterator(base + L"jre")) {
            if (d.is_directory()) {
                std::wstring sp = d.path().wstring() + L"\\bin\\" + (useJavaw ? L"java.exe" : L"javaw.exe");
                if (fs::exists(sp)) return sp;
            }
        }
    }
    return L"";
}

std::string ReadFileToString(const std::wstring& path) {
    std::ifstream f(path); if (!f) return "";
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string ExtractJsonString(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t pos = json.find(searchKey); if (pos == std::string::npos) return "";
    size_t colon = json.find(':', pos + searchKey.length()); if (colon == std::string::npos) return "";
    size_t qs = json.find('"', colon + 1); if (qs == std::string::npos) return "";
    size_t qe = json.find('"', qs + 1); if (qe == std::string::npos) return "";
    return json.substr(qs + 1, qe - qs - 1);
}

bool DownloadTextFromUrl(const std::wstring& url, std::string& content) {
    content.clear();
    HINTERNET hI = InternetOpenW(L"Launcher/1.0", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hI) { SendError(L"InternetOpen failed"); return false; }

    DWORD openFlags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_KEEP_CONNECTION;
    if (url.rfind(L"https://", 0) == 0) openFlags |= INTERNET_FLAG_SECURE;

    HINTERNET hU = InternetOpenUrlW(hI, url.c_str(), NULL, 0, openFlags, 0);
    if (!hU) {
        DWORD e = GetLastError();
        SendError(L"InternetOpenUrl failed: " + std::to_wstring(e));
        InternetCloseHandle(hI);
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    if (HttpQueryInfoW(hU, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusCodeSize, NULL)) {
        if (statusCode < 200 || statusCode >= 300) {
            SendError(L"HTTP error " + std::to_wstring(statusCode) + L" for URL: " + url);
            InternetCloseHandle(hU);
            InternetCloseHandle(hI);
            return false;
        }
    }

    char buf[8192];
    while (true) {
        DWORD br = 0;
        if (!InternetReadFile(hU, buf, sizeof(buf), &br)) {
            DWORD e = GetLastError();
            SendError(L"InternetReadFile failed: " + std::to_wstring(e));
            InternetCloseHandle(hU);
            InternetCloseHandle(hI);
            return false;
        }
        if (br == 0) break;
        content.append(buf, buf + br);
    }

    InternetCloseHandle(hU);
    InternetCloseHandle(hI);
    return true;
}

bool DownloadFileIfMissing(const std::wstring& url, const std::wstring& destPath, const std::wstring& statusText) {
    if (fs::exists(destPath)) return true;
    return DownloadFile(url, destPath, statusText);
}

bool IsLibraryAllowedForWindows(const json& lib) {
    if (!lib.is_object() || !lib.contains("rules") || !lib["rules"].is_array()) return true;

    bool allowed = false;
    for (const auto& rule : lib["rules"]) {
        if (!rule.is_object()) continue;
        bool applies = true;
        if (rule.contains("os") && rule["os"].is_object()) {
            const auto& os = rule["os"];
            if (os.contains("name") && os["name"].is_string()) {
                std::string osName = os["name"].get<std::string>();
                if (osName != "windows") applies = false;
            }
        }
        if (!applies) continue;

        std::string action = rule.value("action", "disallow");
        allowed = (action == "allow");
    }
    return allowed;
}

std::wstring GetLatestStableFabricInstallerUrl() {
    try {
        std::string text;
        if (!DownloadTextFromUrl(L"https://meta.fabricmc.net/v2/versions/installer", text)) return FABRIC_INSTALLER_URL_FALLBACK;
        json arr = json::parse(text, nullptr, false);
        if (!arr.is_array()) return FABRIC_INSTALLER_URL_FALLBACK;
        for (const auto& item : arr) {
            if (!item.is_object()) continue;
            if (item.value("stable", false) && item.contains("url") && item["url"].is_string()) {
                return Utf8ToWide(item["url"].get<std::string>());
            }
        }
    }
    catch (...) {}
    return FABRIC_INSTALLER_URL_FALLBACK;
}

std::string GetLatestStableFabricLoaderVersion() {
    try {
        std::string text;
        if (!DownloadTextFromUrl(L"https://meta.fabricmc.net/v2/versions/loader/" + MC_VERSION, text)) return WideToUtf8(FABRIC_LOADER_FALLBACK);
        json arr = json::parse(text, nullptr, false);
        if (!arr.is_array()) return WideToUtf8(FABRIC_LOADER_FALLBACK);
        for (const auto& item : arr) {
            if (!item.is_object()) continue;
            if (item.value("stable", false) && item.contains("version") && item["version"].is_string()) {
                return item["version"].get<std::string>();
            }
        }
        if (!arr.empty() && arr[0].is_object() && arr[0].contains("version") && arr[0]["version"].is_string()) {
            return arr[0]["version"].get<std::string>();
        }
    }
    catch (...) {}
    return WideToUtf8(FABRIC_LOADER_FALLBACK);
}

bool DownloadMinecraftFromOfficialManifest() {
    try {
        SendProgress(0, 0, 0, g_LangRu ? L"\u041F\u043E\u043B\u0443\u0447\u0435\u043D\u0438\u0435 official manifest..." : L"Fetching official manifest...");

        std::string manifestText;
        if (!DownloadTextFromUrl(MOJANG_VERSION_MANIFEST_URL, manifestText)) return false;
        json manifest = json::parse(manifestText, nullptr, false);
        if (!manifest.is_object() || !manifest.contains("versions") || !manifest["versions"].is_array()) {
            SendError(L"Invalid Mojang version manifest");
            return false;
        }

        std::string mcVersion = WideToUtf8(MC_VERSION);
        std::string versionJsonUrl;
        for (const auto& versionEntry : manifest["versions"]) {
            if (!versionEntry.is_object()) continue;
            if (versionEntry.value("id", std::string()) == mcVersion) {
                versionJsonUrl = versionEntry.value("url", std::string());
                break;
            }
        }

        if (versionJsonUrl.empty()) {
            SendError(L"Minecraft version not found in official manifest: " + MC_VERSION);
            return false;
        }

        fs::create_directories(GetVanillaVersionDir());
        if (!DownloadFileIfMissing(Utf8ToWide(versionJsonUrl), GetVanillaVersionJsonPath(), g_LangRu ? L"\u0421\u043A\u0430\u0447\u0438\u0432\u0430\u043D\u0438\u0435 version JSON..." : L"Downloading version JSON...")) return false;

        std::string versionText = ReadFileToString(GetVanillaVersionJsonPath());
        json versionJson = json::parse(versionText, nullptr, false);
        if (!versionJson.is_object()) {
            SendError(L"Invalid version JSON");
            return false;
        }

        if (!versionJson.contains("downloads") || !versionJson["downloads"].is_object() ||
            !versionJson["downloads"].contains("client") || !versionJson["downloads"]["client"].is_object()) {
            SendError(L"Version JSON is missing client download info");
            return false;
        }

        std::string clientUrl = versionJson["downloads"]["client"].value("url", std::string());
        if (clientUrl.empty()) {
            SendError(L"Client JAR URL is missing in version JSON");
            return false;
        }
        if (!DownloadFileIfMissing(Utf8ToWide(clientUrl), GetVanillaVersionJarPath(), g_LangRu ? L"\u0421\u043A\u0430\u0447\u0438\u0432\u0430\u043D\u0438\u0435 client.jar..." : L"Downloading client.jar...")) return false;

        std::wstring librariesDir = GetMinecraftDir() + L"libraries\\";
        fs::create_directories(librariesDir);
        if (versionJson.contains("libraries") && versionJson["libraries"].is_array()) {
            size_t libIndex = 0;
            size_t libCount = versionJson["libraries"].size();
            for (const auto& lib : versionJson["libraries"]) {
                if (g_CancelDownload) return false;
                libIndex++;

                if (!IsLibraryAllowedForWindows(lib)) continue;
                if (!lib.contains("downloads") || !lib["downloads"].is_object()) continue;
                const auto& downloads = lib["downloads"];

                if (downloads.contains("artifact") && downloads["artifact"].is_object()) {
                    std::string path = downloads["artifact"].value("path", std::string());
                    std::string url = downloads["artifact"].value("url", std::string());
                    if (!path.empty() && !url.empty()) {
                        std::replace(path.begin(), path.end(), '/', '\\');
                        std::wstring dest = librariesDir + Utf8ToWide(path);
                        std::wstring status = (g_LangRu ? L"\u0421\u043A\u0430\u0447\u0438\u0432\u0430\u043D\u0438\u0435 library " : L"Downloading library ") + std::to_wstring(libIndex) + L"/" + std::to_wstring(libCount);
                        if (!DownloadFileIfMissing(Utf8ToWide(url), dest, status)) return false;
                    }
                }

                if (downloads.contains("classifiers") && downloads["classifiers"].is_object()) {
                    for (auto it = downloads["classifiers"].begin(); it != downloads["classifiers"].end(); ++it) {
                        std::string classifierName = it.key();
                        if (classifierName.rfind("natives-windows", 0) != 0) continue;
                        if (!it.value().is_object()) continue;
                        std::string path = it.value().value("path", std::string());
                        std::string url = it.value().value("url", std::string());
                        if (path.empty() || url.empty()) continue;
                        std::replace(path.begin(), path.end(), '/', '\\');
                        std::wstring dest = librariesDir + Utf8ToWide(path);
                        std::wstring status = (g_LangRu ? L"\u0421\u043A\u0430\u0447\u0438\u0432\u0430\u043D\u0438\u0435 native library " : L"Downloading native library ") + std::to_wstring(libIndex) + L"/" + std::to_wstring(libCount);
                        if (!DownloadFileIfMissing(Utf8ToWide(url), dest, status)) return false;
                    }
                }
            }
        }

        if (!versionJson.contains("assetIndex") || !versionJson["assetIndex"].is_object()) {
            SendError(L"Version JSON is missing asset index info");
            return false;
        }
        std::string assetIndexId = versionJson["assetIndex"].value("id", std::string());
        std::string assetIndexUrl = versionJson["assetIndex"].value("url", std::string());
        if (assetIndexId.empty() || assetIndexUrl.empty()) {
            SendError(L"Asset index id/url is missing");
            return false;
        }

        std::wstring assetIndexesDir = GetMinecraftDir() + L"assets\\indexes\\";
        std::wstring assetObjectsDir = GetMinecraftDir() + L"assets\\objects\\";
        fs::create_directories(assetIndexesDir);
        fs::create_directories(assetObjectsDir);
        std::wstring assetIndexPath = assetIndexesDir + Utf8ToWide(assetIndexId) + L".json";
        if (!DownloadFileIfMissing(Utf8ToWide(assetIndexUrl), assetIndexPath, g_LangRu ? L"\u0421\u043A\u0430\u0447\u0438\u0432\u0430\u043D\u0438\u0435 asset index..." : L"Downloading asset index...")) return false;

        std::string assetsText = ReadFileToString(assetIndexPath);
        json assetsJson = json::parse(assetsText, nullptr, false);
        if (!assetsJson.is_object() || !assetsJson.contains("objects") || !assetsJson["objects"].is_object()) {
            SendError(L"Invalid asset index JSON");
            return false;
        }

        const auto& objects = assetsJson["objects"];
        size_t totalAssets = objects.size();
        size_t assetCounter = 0;
        for (auto it = objects.begin(); it != objects.end(); ++it) {
            if (g_CancelDownload) return false;
            assetCounter++;

            if (!it.value().is_object()) continue;
            std::string hash = it.value().value("hash", std::string());
            if (hash.length() < 2) continue;

            std::string subdir = hash.substr(0, 2);
            std::wstring objPath = assetObjectsDir + Utf8ToWide(subdir) + L"\\" + Utf8ToWide(hash);
            if (fs::exists(objPath)) continue;

            std::wstring objUrl = Utf8ToWide(std::string("https://resources.download.minecraft.net/") + subdir + "/" + hash);
            if (!DownloadFile(objUrl, objPath, g_LangRu ? L"\u0421\u043A\u0430\u0447\u0438\u0432\u0430\u043D\u0438\u0435 assets..." : L"Downloading assets...")) return false;

            if (assetCounter % 50 == 0 || assetCounter == totalAssets) {
                double percent = totalAssets > 0 ? (double)assetCounter * 100.0 / (double)totalAssets : 100.0;
                SendProgress(percent, (double)assetCounter, (double)totalAssets, g_LangRu ? L"\u0421\u043A\u0430\u0447\u0430\u043D\u043E assets" : L"Assets downloaded");
            }
        }
    }
    catch (const std::exception& ex) {
        SendError(L"Official manifest install failed: " + Utf8ToWide(ex.what()));
        return false;
    }
    catch (...) {
        SendError(L"Official manifest install failed with unknown error");
        return false;
    }

    return true;
}

bool InstallFabricClientWithInstaller(const std::wstring& javaExe) {
    std::wstring installerUrl = GetLatestStableFabricInstallerUrl();
    std::string loaderVersion = GetLatestStableFabricLoaderVersion();
    std::wstring installerJarPath = GetBaseDir() + L"fabric-installer.jar";
    std::wstring minecraftDir = GetMinecraftDir();
    if (!minecraftDir.empty() && (minecraftDir.back() == L'\\' || minecraftDir.back() == L'/')) minecraftDir.pop_back();

    if (!DownloadFile(installerUrl, installerJarPath, g_LangRu ? L"\u0421\u043A\u0430\u0447\u0438\u0432\u0430\u043D\u0438\u0435 Fabric installer..." : L"Downloading Fabric installer...")) return false;

    std::wstring cmd = L"\"" + javaExe + L"\" -jar \"" + installerJarPath + L"\" client"
        + L" -dir \"" + minecraftDir + L"\""
        + L" -mcversion \"" + MC_VERSION + L"\""
        + L" -loader \"" + Utf8ToWide(loaderVersion) + L"\""
        + L" -noprofile";

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(0);

    if (!CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        DWORD e = GetLastError();
        SendError(L"Failed to start Fabric installer: " + std::to_wstring(e));
        return false;
    }

    while (true) {
        DWORD waitResult = WaitForSingleObject(pi.hProcess, 200);
        if (waitResult == WAIT_OBJECT_0) break;
        if (g_CancelDownload) {
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            SendError(g_LangRu ? L"\u0423\u0441\u0442\u0430\u043D\u043E\u0432\u043A\u0430 Fabric \u043E\u0442\u043C\u0435\u043D\u0435\u043D\u0430" : L"Fabric install cancelled");
            return false;
        }
    }
    DWORD ec = 0;
    GetExitCodeProcess(pi.hProcess, &ec);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (ec != 0) {
        SendError(L"Fabric installer exited with code: " + std::to_wstring(ec));
        return false;
    }

    fs::remove(installerJarPath);
    return true;
}

std::string BuildClasspath() {
    std::wstring vj = GetVersionJarPath();
    std::wstring libDir = GetMinecraftDir() + L"libraries";
    std::string cp;
    if (fs::exists(vj)) cp = WideToUtf8(vj);

    auto parseVersion = [](const std::string& ver) -> std::vector<int> {
        std::vector<int> nums; std::string current;
        for (size_t i = 0; i < ver.size(); i++) {
            char c = ver[i];
            if (c == '.' || c == '-' || c == '_' || c == '+') {
                if (!current.empty()) { try { nums.push_back(std::stoi(current)); } catch (...) { nums.push_back(0); } current.clear(); }
            }
            else if (c >= '0' && c <= '9') { current += c; }
            else { if (!current.empty()) { try { nums.push_back(std::stoi(current)); } catch (...) { nums.push_back(0); } current.clear(); } }
        }
        if (!current.empty()) { try { nums.push_back(std::stoi(current)); } catch (...) { nums.push_back(0); } }
        while (nums.size() < 4) nums.push_back(0); return nums;
        };

    auto isVersionGreater = [&parseVersion](const std::string& a, const std::string& b) -> bool {
        auto va = parseVersion(a), vb = parseVersion(b);
        size_t len = va.size() < vb.size() ? va.size() : vb.size();
        for (size_t i = 0; i < len; i++) { if (va[i] > vb[i]) return true; if (va[i] < vb[i]) return false; }
        return false;
        };

    struct LibEntry { std::string key; std::string version; std::wstring fullPath; };
    std::map<std::string, LibEntry> bestLibs;

    if (fs::exists(libDir)) {
        for (auto const& entry : fs::recursive_directory_iterator(libDir)) {
            if (!entry.is_regular_file()) continue;
            std::wstring ext = entry.path().extension().wstring();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
            if (ext != L".jar") continue;
            std::wstring fullPath = entry.path().wstring();
            std::string fileName = WideToUtf8(entry.path().filename().wstring());
            if (fileName.find("natives-linux") != std::string::npos) continue;
            if (fileName.find("natives-macos") != std::string::npos) continue;
            if (fileName.find("natives-osx") != std::string::npos) continue;
            if (fileName.find("linux-aarch") != std::string::npos) continue;
            if (fileName.find("linux-x86_64") != std::string::npos) continue;
            std::wstring relPath = fullPath.substr(libDir.length());
            if (!relPath.empty() && (relPath[0] == L'\\' || relPath[0] == L'/')) relPath = relPath.substr(1);
            std::string relUtf8 = WideToUtf8(relPath);
            std::replace(relUtf8.begin(), relUtf8.end(), '\\', '/');
            std::vector<std::string> pathParts; std::istringstream ss(relUtf8); std::string tok;
            while (std::getline(ss, tok, '/')) { if (!tok.empty()) pathParts.push_back(tok); }
            std::string key, version;
            if (pathParts.size() >= 4) {
                version = pathParts[pathParts.size() - 2];
                std::string artifact = pathParts[pathParts.size() - 3]; std::string group;
                for (size_t i = 0; i < pathParts.size() - 3; i++) { if (!group.empty()) group += "."; group += pathParts[i]; }
                key = group + ":" + artifact;
                if (fileName.find("natives-windows") != std::string::npos) {
                    key += ":natives-windows";
                    if (fileName.find("arm64") != std::string::npos) key += "-arm64";
                    else if (fileName.find("x86") != std::string::npos && fileName.find("x86_64") == std::string::npos) key += "-x86";
                }
            }
            else { key = WideToUtf8(fullPath); version = "0"; }
            auto it = bestLibs.find(key);
            if (it == bestLibs.end()) { LibEntry le; le.key = key; le.version = version; le.fullPath = fullPath; bestLibs[key] = le; }
            else { if (isVersionGreater(version, it->second.version)) { it->second.version = version; it->second.fullPath = fullPath; } }
        }
    }
    for (auto it = bestLibs.begin(); it != bestLibs.end(); ++it) {
        if (!cp.empty()) cp += ";";
        cp += WideToUtf8(it->second.fullPath);
    }
    return cp;
}

std::string GetMainClass() {
    std::wstring jp = GetVersionJsonPath();
    if (fs::exists(jp)) { std::string jc = ReadFileToString(jp); std::string mc = ExtractJsonString(jc, "mainClass"); if (!mc.empty()) return mc; }
    return "net.fabricmc.loader.impl.launch.knot.KnotClient";
}

std::string GetAssetIndex() {
    std::wstring jp = GetVersionJsonPath();
    if (fs::exists(jp)) {
        std::string jc = ReadFileToString(jp);
        size_t aiPos = jc.find("\"assetIndex\"");
        if (aiPos != std::string::npos) {
            size_t braceStart = jc.find('{', aiPos);
            if (braceStart != std::string::npos) {
                size_t braceEnd = jc.find('}', braceStart);
                if (braceEnd != std::string::npos) {
                    std::string aiBlock = jc.substr(braceStart, braceEnd - braceStart + 1);
                    std::string id = ExtractJsonString(aiBlock, "id"); if (!id.empty()) return id;
                }
            }
        }
        std::string assets = ExtractJsonString(jc, "assets"); if (!assets.empty()) return assets;
    }
    std::wstring vanillaJson = GetVanillaVersionJsonPath();
    if (fs::exists(vanillaJson)) {
        std::string jc = ReadFileToString(vanillaJson);
        size_t aiPos = jc.find("\"assetIndex\"");
        if (aiPos != std::string::npos) {
            size_t braceStart = jc.find('{', aiPos);
            if (braceStart != std::string::npos) {
                size_t braceEnd = jc.find('}', braceStart);
                if (braceEnd != std::string::npos) {
                    std::string aiBlock = jc.substr(braceStart, braceEnd - braceStart + 1);
                    std::string id = ExtractJsonString(aiBlock, "id"); if (!id.empty()) return id;
                }
            }
        }
        std::string assets = ExtractJsonString(jc, "assets"); if (!assets.empty()) return assets;
    }
    return WideToUtf8(MC_VERSION);
}

void MonitorProcessThread(DWORD pid) {
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid); if (h) { WaitForSingleObject(h, INFINITE); CloseHandle(h); }
    g_GamePID = 0; SafePostJson(L"{ \"type\": \"process_stopped\" }");
}

void LogCommandLine(const std::wstring& cmd) {
    std::wstring logPath = GetBaseDir() + L"launch_cmd.log";
    std::ofstream f(logPath, std::ios::trunc); if (f) { f << WideToUtf8(cmd); f.close(); }
}

int GetSafeRamAmount(int requested) {
    if (requested < 1024) {
        SendError(L"RAM value is too low, clamped to 1024MB");
        requested = 1024;
    }
    MEMORYSTATUSEX memInfo; memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        int totalMb = (int)(memInfo.ullTotalPhys / 1024 / 1024);
        int maxAllowed = totalMb - 1024; if (maxAllowed < 1024) maxAllowed = 1024;
        maxAllowed = (maxAllowed / 128) * 128;
        if (requested > maxAllowed) {
            SendError(L"RAM " + std::to_wstring(requested) + L"MB exceeds available (" + std::to_wstring(totalMb) + L"MB total), clamped to " + std::to_wstring(maxAllowed) + L"MB");
            return maxAllowed;
        }
    }
    return requested;
}

std::wstring GetSafeNickname() {
    std::wstring nick = g_Nickname; std::wstring safe;
    for (wchar_t c : nick) {
        if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'_') safe += c;
    }
    if (safe.empty()) safe = L"Player"; if (safe.length() > 16) safe = safe.substr(0, 16);
    return safe;
}

void ResizeWindow(bool expanded) {
    if (!g_hWnd) return;
    RECT rc; GetWindowRect(g_hWnd, &rc);
    int newW = expanded ? (MAIN_WIDTH + EXTRA_WIDTH) : MAIN_WIDTH;
    SetWindowPos(g_hWnd, NULL, rc.left, rc.top, newW, MAIN_HEIGHT, SWP_NOZORDER | SWP_NOACTIVATE);
    if (g_webviewController) {
        RECT b; GetClientRect(g_hWnd, &b);
        g_webviewController->put_Bounds(b);
    }
    g_ExtraPanelOpen = expanded;
}

void LaunchGame() {
    std::wstring javawExe = FindJavaExe(true);
    if (javawExe.empty()) javawExe = FindJavaExe(false);
    if (javawExe.empty()) { SendError(L"No Java executable found"); return; }

    int safeRam = GetSafeRamAmount(g_RamAmount);
    std::string cpStr = BuildClasspath();
    if (cpStr.empty()) { SendError(L"Classpath is empty"); return; }
    std::string mc = GetMainClass();
    std::string assetIndex = GetAssetIndex();
    std::wstring md = GetMinecraftDir();
    std::wstring nd = GetVersionDir() + L"natives";
    std::wstring activeProfile = GetActiveFabricProfileName();
    if (!fs::exists(nd)) fs::create_directories(nd);
    if (!md.empty() && md.back() == L'\\') md.pop_back();
    if (!nd.empty() && nd.back() == L'\\') nd.pop_back();
    std::wstring assetsDir = GetMinecraftDir() + L"assets";
    if (!assetsDir.empty() && assetsDir.back() == L'\\') assetsDir.pop_back();

    std::wstring safeNick = GetSafeNickname();

    std::vector<std::wstring> jvmArgs;
    jvmArgs.push_back(L"-Xmx" + std::to_wstring(safeRam) + L"M");
    jvmArgs.push_back(L"-Xms512M"); jvmArgs.push_back(L"-Xss1M");
    jvmArgs.push_back(L"-XX:+UseG1GC"); jvmArgs.push_back(L"-XX:+UnlockExperimentalVMOptions");
    jvmArgs.push_back(L"-XX:G1NewSizePercent=20"); jvmArgs.push_back(L"-XX:G1ReservePercent=20");
    jvmArgs.push_back(L"-XX:MaxGCPauseMillis=50"); jvmArgs.push_back(L"-XX:G1HeapRegionSize=32M");
    jvmArgs.push_back(L"-XX:HeapDumpPath=MojangTricksIntelDriversForPerformance_javaw.exe_minecraft.exe.heapdump");
    jvmArgs.push_back(L"-Djava.library.path=" + nd);
    jvmArgs.push_back(L"-Djna.tmpdir=" + nd);
    jvmArgs.push_back(L"-Dorg.lwjgl.system.SharedLibraryExtractPath=" + nd);
    jvmArgs.push_back(L"-Dio.netty.native.workdir=" + nd);
    jvmArgs.push_back(L"-Dminecraft.launcher.brand=custom-launcher");
    jvmArgs.push_back(L"-Dminecraft.launcher.version=1.0");
    jvmArgs.push_back(L"-DFabricMcEmu=net.minecraft.client.main.Main");
    jvmArgs.push_back(L"-cp"); jvmArgs.push_back(Utf8ToWide(cpStr));

    std::vector<std::wstring> gameArgs;
    gameArgs.push_back(L"--username"); gameArgs.push_back(safeNick);
    gameArgs.push_back(L"--version"); gameArgs.push_back(activeProfile);
    gameArgs.push_back(L"--gameDir"); gameArgs.push_back(md);
    gameArgs.push_back(L"--assetsDir"); gameArgs.push_back(assetsDir);
    gameArgs.push_back(L"--assetIndex"); gameArgs.push_back(Utf8ToWide(assetIndex));
    gameArgs.push_back(L"--uuid"); gameArgs.push_back(L"00000000-0000-0000-0000-000000000000");
    gameArgs.push_back(L"--accessToken"); gameArgs.push_back(L"0");
    gameArgs.push_back(L"--userType"); gameArgs.push_back(L"legacy");
    gameArgs.push_back(L"--versionType"); gameArgs.push_back(L"release");

    auto quoteIfNeeded = [](const std::wstring& s) -> std::wstring {
        if (s.find(L' ') != std::wstring::npos || s.find(L'\t') != std::wstring::npos) return L"\"" + s + L"\"";
        return s;
        };

    std::wstring cmd = quoteIfNeeded(javawExe);
    for (auto& a : jvmArgs) cmd += L" " + quoteIfNeeded(a);
    cmd += L" " + Utf8ToWide(mc);
    for (auto& a : gameArgs) cmd += L" " + quoteIfNeeded(a);
    LogCommandLine(cmd);

    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end()); cmdBuf.push_back(0);
    std::wstring elp = GetBaseDir() + L"launch_error.log";
    SECURITY_ATTRIBUTES sa; sa.nLength = sizeof(sa); sa.lpSecurityDescriptor = NULL; sa.bInheritHandle = TRUE;
    HANDLE hLog = CreateFileW(elp.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    STARTUPINFOW si = { sizeof(si) };
    if (hLog != INVALID_HANDLE_VALUE) { si.dwFlags |= STARTF_USESTDHANDLES; si.hStdError = hLog; si.hStdOutput = hLog; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE); }
    PROCESS_INFORMATION pi;
    if (CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, TRUE, 0, NULL, md.c_str(), &si, &pi)) {
        g_GamePID = pi.dwProcessId;
        HANDLE hPC = pi.hProcess, hLC = hLog; std::wstring elpCopy = elp;
        std::thread([hPC, hLC, elpCopy]() {
            WaitForSingleObject(hPC, 30000); DWORD exitCode = 0; GetExitCodeProcess(hPC, &exitCode);
            if (exitCode != STILL_ACTIVE && exitCode != 0) {
                CloseHandle(hPC); if (hLC != INVALID_HANDLE_VALUE) CloseHandle(hLC);
                std::wstring errMsg = L"Java exited code " + std::to_wstring(exitCode);
                if (fs::exists(elpCopy) && fs::file_size(elpCopy) > 0) {
                    std::string content = ReadFileToString(elpCopy);
                    std::istringstream iss(content); std::vector<std::string> lines; std::string line;
                    while (std::getline(iss, line)) lines.push_back(line);
                    int start = (int)lines.size() > 10 ? (int)lines.size() - 10 : 0; std::string last;
                    for (int i = start; i < (int)lines.size(); i++) {
                        std::string c = lines[i]; size_t p;
                        while ((p = c.find('"')) != std::string::npos) c.replace(p, 1, "'");
                        while ((p = c.find('\\')) != std::string::npos) c.replace(p, 1, "/");
                        last += c + " | ";
                    }
                    if (!last.empty()) errMsg += L" :: " + Utf8ToWide(last);
                }
                SendError(errMsg);
            }
            else { CloseHandle(hPC); if (hLC != INVALID_HANDLE_VALUE) CloseHandle(hLC); }
            }).detach();
        std::thread(MonitorProcessThread, pi.dwProcessId).detach();
        CloseHandle(pi.hThread);
    }
    else { DWORD e = GetLastError(); if (hLog != INVALID_HANDLE_VALUE) CloseHandle(hLog); SendError(L"CreateProcess failed: " + std::to_wstring(e)); }
}

void TerminateGame() {
    DWORD pid = g_GamePID; if (pid != 0) {
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid); if (h) { TerminateProcess(h, 0); CloseHandle(h); } g_GamePID = 0;
    }
}

void InstallAndLaunchThread() {
    struct InstallGuard {
        ~InstallGuard() { g_InstallInProgress = false; }
    } installGuard;

    g_CancelDownload = false;
    try {
        std::wstring base = GetBaseDir(), md = GetMinecraftDir(), modd = GetModsDir();
        fs::create_directories(base); fs::create_directories(md); fs::create_directories(modd);

        bool ok = true;
        if (ok) ok = DownloadFile(JRE_URL, base + L"jre.zip", g_LangRu ? L"\u0421\u043A\u0430\u0447\u0438\u0432\u0430\u043D\u0438\u0435 Java..." : L"Downloading Java...");
        if (ok) {
            SendProgress(100, 0, 0, g_LangRu ? L"\u0420\u0430\u0441\u043F\u0430\u043A\u043E\u0432\u043A\u0430 Java..." : L"Extracting Java...");
            if (fs::exists(base + L"jre")) fs::remove_all(base + L"jre");
            ok = UnzipWithPowerShell(base + L"jre.zip", base + L"jre"); fs::remove(base + L"jre.zip");
        }
        if (ok) ok = DownloadFile(MOD_URL, modd + MOD_FILE_NAME, g_LangRu ? L"\u0421\u043A\u0430\u0447\u0438\u0432\u0430\u043D\u0438\u0435 \u043C\u043E\u0434\u0430..." : L"Downloading mod...");
        if (ok) ok = DownloadFile(ADD_MOD_URL, modd + FABRIC_API_FILE_NAME, g_LangRu ? L"\u0421\u043A\u0430\u0447\u0438\u0432\u0430\u043D\u0438\u0435 Fabric API..." : L"Downloading Fabric API...");
        if (ok) ok = DownloadMinecraftFromOfficialManifest();
        if (ok) {
            std::wstring je = FindJavaExe(false);
            if (je.empty()) je = FindJavaExe(true);
            if (je.empty()) { SendError(L"Java not found after install"); ok = false; }
            else ok = InstallFabricClientWithInstaller(je);
        }

        if (g_CancelDownload) {
            SendError(g_LangRu ? L"\u0423\u0441\u0442\u0430\u043D\u043E\u0432\u043A\u0430 \u043E\u0442\u043C\u0435\u043D\u0435\u043D\u0430 \u043F\u043E\u043B\u044C\u0437\u043E\u0432\u0430\u0442\u0435\u043B\u0435\u043C" : L"Installation cancelled by user");
            SafePostJson(L"{ \"type\": \"install_canceled\" }");
            SaveRegistry(false, g_RamAmount, g_DarkTheme, g_LangRu, g_Nickname);
            return;
        }

        if (ok) {
            std::wstring vjson = GetVersionJsonPath();
            std::wstring vjsonVanilla = GetVanillaVersionJsonPath();
            std::wstring vjar = GetVanillaVersionJarPath();
            if (!fs::exists(vjson)) { SendError(L"Version JSON missing: " + vjson); ok = false; }
            if (!fs::exists(vjsonVanilla)) { SendError(L"Vanilla version JSON missing: " + vjsonVanilla); ok = false; }
            if (!fs::exists(vjar)) { SendError(L"Version JAR missing: " + vjar); ok = false; }
        }

        if (ok) {
            SaveRegistry(true, g_RamAmount, g_DarkTheme, g_LangRu, g_Nickname);
            SafePostJson(L"{ \"type\": \"finish_install\" }");
            std::this_thread::sleep_for(std::chrono::milliseconds(2500));
            LaunchGame();
        }
        else {
            SafePostJson(L"{ \"type\": \"progress\", \"status\": \"Error!\" }");
            SaveRegistry(false, g_RamAmount, g_DarkTheme, g_LangRu, g_Nickname);
        }
    }
    catch (const std::exception& ex) {
        SendError(L"Install failed with exception: " + Utf8ToWide(ex.what()));
        SaveRegistry(false, g_RamAmount, g_DarkTheme, g_LangRu, g_Nickname);
    }
    catch (...) {
        SendError(L"Install failed with unknown exception");
        SaveRegistry(false, g_RamAmount, g_DarkTheme, g_LangRu, g_Nickname);
    }
}

void StartProcessLogic() {
    if (g_GamePID != 0) { TerminateGame(); return; }
    if (g_InstallInProgress) {
        SendError(g_LangRu ? L"\u0423\u0441\u0442\u0430\u043D\u043E\u0432\u043A\u0430 \u0443\u0436\u0435 \u0438\u0434\u0451\u0442" : L"Installation is already in progress");
        return;
    }
    RegState st = LoadRegistry();
    bool fe = fs::exists(GetVersionJsonPath()) && fs::exists(GetVanillaVersionJsonPath()) && fs::exists(GetVanillaVersionJarPath()) && fs::exists(GetBaseDir() + L"jre");
    if (st.isInstalled && fe) { SafePostJson(L"{ \"type\": \"launch_success\" }"); LaunchGame(); }
    else {
        g_InstallInProgress = true;
        SafePostJson(L"{ \"type\": \"start_load\" }");
        try {
            std::thread(InstallAndLaunchThread).detach();
        }
        catch (...) {
            g_InstallInProgress = false;
            SendError(L"Failed to start install thread");
        }
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_WEBVIEW_UPDATE: { std::wstring* j = (std::wstring*)lParam; if (g_webview) g_webview->PostWebMessageAsJson(j->c_str()); delete j; return 0; }
    case WM_SIZE: if (g_webviewController) { RECT b; GetClientRect(hWnd, &b); g_webviewController->put_Bounds(b); } break;
    case WM_NCHITTEST: { POINT p = { LOWORD(lParam), HIWORD(lParam) }; ScreenToClient(hWnd, &p); if (p.y < 45 && p.x < 302) return HTCAPTION; return DefWindowProc(hWnd, message, wParam, lParam); }
    case WM_DESTROY: g_CancelDownload = true; TerminateGame(); PostQuitMessage(0); break;
    default: return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    RegState saved = LoadRegistry();
    g_DarkTheme = saved.darkTheme; g_LangRu = saved.langRu; g_RamAmount = saved.ram;
    g_HasSavedPrefs = saved.hasPrefs; g_Nickname = saved.nickname;

    std::wstring themeColor = L"#7BCC87";
    std::wstring themeColorRgb = L"123, 204, 135";
    std::wstring cheatNameUpper = CHEAT_NAME;
    std::transform(cheatNameUpper.begin(), cheatNameUpper.end(), cheatNameUpper.begin(), ::towupper);

    std::wstring css1 = LR"CSS(
<style>
:root{--green:)CSS" + themeColor + LR"CSS(;--bg:#111;--bg-dark:#050505;--bg-light:#F5F5F5;--bg-light-card:#FFF;--border:#2C2B2B;--border-light:#E0E0E0;--text-white:#FFF;--text-dark:#111;--orange:#FF9D00;--red:#FF6200;--btn-red:#D93025;--theme-rgb:)CSS" + themeColorRgb + LR"CSS(;--main-w:382px;--extra-w:220px;}
*{box-sizing:border-box;}
body{margin:0;padding:0;display:flex;justify-content:flex-start;align-items:center;height:100vh;font-family:'Montserrat',sans-serif;overflow:hidden;user-select:none;transition:background-color .4s;}
body.dark{background-color:var(--bg-dark);color:var(--text-white);}
body.light{background-color:var(--bg-light);color:var(--text-dark);}
.outer-container{display:flex;height:532px;position:relative;}
.wrapper{position:relative;width:var(--main-w);min-width:var(--main-w);height:532px;overflow:hidden;transition:background .4s,border-color .4s,box-shadow .4s;flex-shrink:0;}
body.dark .wrapper{background:var(--bg);border:1px solid var(--border);box-shadow:0 20px 60px rgba(0,0,0,.8);}
body.light .wrapper{background:var(--bg-light-card);border:1px solid var(--border-light);box-shadow:0 20px 60px rgba(0,0,0,.15);}
.title-drag-area{position:absolute;top:0;left:0;width:100%;height:40px;z-index:999;cursor:default;}
.screen{position:absolute;top:0;left:0;width:100%;height:100%;transition:transform .6s cubic-bezier(.22,1,.36,1),opacity .4s,background .4s;display:flex;flex-direction:column;}
body.dark .screen{background:var(--bg);}body.light .screen{background:var(--bg-light-card);}
.screen.active{transform:translateX(0);opacity:1;z-index:2;pointer-events:all;}
.screen.inactive-left{transform:translateX(-100px) scale(.95);opacity:0;z-index:1;pointer-events:none;filter:blur(5px);}
.screen.inactive-right{transform:translateX(100%);opacity:0;z-index:1;pointer-events:none;}
.text-green{color:var(--green);}
body.dark .text-main{color:var(--text-white);}body.light .text-main{color:var(--text-dark);}
body.dark .text-faint{color:rgba(255,255,255,.2);}body.light .text-faint{color:rgba(0,0,0,.3);}
.font-unbounded{font-family:'Unbounded',sans-serif;font-weight:500;}
.font-medium{font-weight:500;}.font-semibold{font-weight:600;}
.window-controls{position:absolute;top:14px;right:14px;display:flex;gap:8px;z-index:1000;cursor:pointer;}
.dot{width:18px;height:18px;border-radius:50%;transition:opacity .2s;}.dot:hover{opacity:.8;}
.dot-orange{background:var(--orange);}.dot-red{background:var(--red);}
.header-title{position:absolute;top:40px;left:30px;display:flex;align-items:center;gap:12px;font-size:26px;line-height:32px;}
.logo-icon{width:32px;height:32px;fill:var(--green);filter:drop-shadow(0 0 5px rgba(var(--theme-rgb),.3));}
.version-row{position:absolute;top:90px;left:30px;font-size:22px;white-space:nowrap;}
)CSS";

    std::wstring css2 = LR"CSS(
.image-frame{position:absolute;top:125px;left:30px;width:322px;height:140px;border-radius:16px;background-color:#333;background-image:url('https://s4.iimage.su/s/08/geyw6HWxcE3UZ5b8Mfpu7SXbSsfCqNn4PWw8q8h6.jpg');background-size:cover;background-position:center;}
.description{position:absolute;top:280px;left:30px;width:322px;font-size:14px;line-height:18px;}
.credits{position:absolute;top:315px;left:30px;font-size:11px;line-height:15px;}
.btn-small{position:absolute;height:50px;border-radius:14px;display:flex;align-items:center;justify-content:center;font-size:20px;cursor:pointer;transition:.2s;user-select:none;}
body.dark .btn-small{background:#212121;border:1.4px solid var(--border);}body.light .btn-small{background:#F0F0F0;border:1.4px solid var(--border-light);}
.btn-small:hover{border-color:var(--green);}.btn-small:active{transform:scale(.96);}
.btn-site{width:88px;bottom:90px;left:30px;}.btn-settings{width:222px;bottom:90px;left:130px;}
.btn-launch{position:absolute;bottom:30px;left:30px;width:322px;height:50px;background:var(--green);border-radius:14px;display:flex;align-items:center;justify-content:center;font-size:24px;font-family:'Montserrat',sans-serif;font-weight:600;border:none;cursor:pointer;transition:.3s cubic-bezier(.25,.8,.25,1);}
body.dark .btn-launch{color:var(--bg);}body.light .btn-launch{color:#fff;}
.btn-launch:hover{opacity:.9;transform:translateY(-2px);box-shadow:0 5px 15px rgba(var(--theme-rgb),.3);}
.btn-launch:active{transform:scale(.98);}
.btn-launch.btn-quit-mode{background:var(--btn-red);color:#FFF;box-shadow:0 5px 15px rgba(217,48,37,.3);}
.screen-title{position:absolute;top:40px;left:30px;font-size:26px;font-family:'Unbounded',sans-serif;color:var(--green);}
.nick-group{position:absolute;top:90px;left:30px;width:322px;}
.nick-label{font-size:18px;margin-bottom:8px;display:block;color:var(--green);font-weight:600;}
.nick-row{display:flex;align-items:center;gap:8px;}
.nick-input{width:0;flex:1;height:38px;border-radius:10px;font-size:16px;font-family:'Montserrat',sans-serif;font-weight:600;padding:0 12px;outline:none;transition:border-color .2s;min-width:0;}
body.dark .nick-input{background:#212121;border:1.4px solid var(--border);color:var(--green);}
body.light .nick-input{background:#f0f0f0;border:1.4px solid var(--border-light);color:var(--text-dark);}
.nick-input:focus{border-color:var(--green);}
.nick-input::placeholder{color:#444;}body.light .nick-input::placeholder{color:#aaa;}
.btn-nick-save{flex-shrink:0;width:100px;height:38px;background:var(--green);border-radius:10px;display:flex;align-items:center;justify-content:center;font-size:15px;font-family:'Montserrat',sans-serif;font-weight:600;cursor:pointer;border:none;transition:.2s;}
body.dark .btn-nick-save{color:var(--bg);}body.light .btn-nick-save{color:#fff;}
.btn-nick-save:hover{opacity:.9;box-shadow:0 2px 10px rgba(var(--theme-rgb),.2);}
.btn-nick-save:active{transform:scale(.95);}
.ram-group{position:absolute;top:170px;left:30px;width:322px;}
.ram-header{display:flex;justify-content:space-between;align-items:flex-end;margin-bottom:8px;font-size:18px;}
.slider{-webkit-appearance:none;width:100%;height:12px;border-radius:10px;outline:none;margin:0;cursor:pointer;}
body.dark .slider{background:#212121;border:1.4px solid var(--border);}body.light .slider{background:#E0E0E0;border:1.4px solid var(--border-light);}
.slider::-webkit-slider-thumb{-webkit-appearance:none;width:36px;height:20px;background:var(--green);border-radius:10px;cursor:grab;box-shadow:0 0 10px rgba(var(--theme-rgb),.4);transition:transform .1s;}
body.dark .slider::-webkit-slider-thumb{border:2px solid var(--bg);}body.light .slider::-webkit-slider-thumb{border:2px solid #fff;}
.slider::-webkit-slider-thumb:hover{transform:scale(1.1);}
.btn-extra-settings{position:absolute;top:240px;left:30px;width:322px;height:44px;border-radius:12px;display:flex;align-items:center;justify-content:center;gap:8px;font-size:15px;font-family:'Montserrat',sans-serif;font-weight:600;cursor:pointer;transition:.2s;user-select:none;}
body.dark .btn-extra-settings{background:#1a1a1a;border:1.4px solid var(--border);color:var(--green);}
body.light .btn-extra-settings{background:#f0f0f0;border:1.4px solid var(--border-light);color:var(--green);}
.btn-extra-settings:hover{border-color:var(--green);}
.btn-extra-settings:active{transform:scale(.97);}
.btn-extra-settings svg{width:16px;height:16px;transition:transform .3s;}
.btn-extra-settings.open svg{transform:rotate(180deg);}
)CSS";

    std::wstring css3 = LR"CSS(
.btn-back{position:absolute;bottom:30px;left:30px;width:322px;height:50px;border-radius:14px;display:flex;align-items:center;justify-content:center;font-size:20px;cursor:pointer;transition:.2s;user-select:none;font-family:'Montserrat',sans-serif;font-weight:600;}
body.dark .btn-back{background:#212121;border:1.4px solid var(--border);}body.light .btn-back{background:#F0F0F0;border:1.4px solid var(--border-light);}
.btn-back:hover{border-color:var(--green);}.btn-back:active{transform:scale(.98);}
.btn-cancel{position:absolute;bottom:30px;left:30px;width:322px;height:40px;background:transparent;border-radius:12px;display:flex;align-items:center;justify-content:center;font-size:16px;color:#888;cursor:pointer;transition:.2s;user-select:none;font-family:'Montserrat',sans-serif;font-weight:600;}
body.dark .btn-cancel{border:1px solid var(--border);}body.light .btn-cancel{border:1px solid var(--border-light);}
.btn-cancel:hover{border-color:var(--btn-red);color:var(--btn-red);background:rgba(217,48,37,.05);}
.toast{position:absolute;bottom:40px;left:191px;transform:translateX(-50%) translateY(120px);backdrop-filter:blur(16px);border:1px solid rgba(255,255,255,.05);padding:14px 24px;border-radius:18px;font-size:14px;font-family:'Montserrat',sans-serif;font-weight:600;opacity:0;pointer-events:none;transition:all .6s cubic-bezier(.22,1,.36,1);z-index:100;display:flex;align-items:center;gap:14px;min-width:200px;white-space:nowrap;}
body.dark .toast{background:rgba(18,18,18,.9);color:#fff;box-shadow:0 0 0 1px rgba(0,0,0,1),0 20px 50px rgba(0,0,0,.8);}
body.light .toast{background:rgba(255,255,255,.95);color:#111;box-shadow:0 0 0 1px rgba(0,0,0,.05),0 20px 50px rgba(0,0,0,.15);}
.toast-icon{flex-shrink:0;width:32px;height:32px;background:linear-gradient(135deg,rgba(var(--theme-rgb),.2),rgba(var(--theme-rgb),.05));border:1px solid rgba(var(--theme-rgb),.3);border-radius:10px;display:flex;align-items:center;justify-content:center;color:var(--green);}
.toast-icon svg{width:16px;height:16px;stroke-width:2.5;}
.toast-content{display:flex;flex-direction:column;gap:2px;}
.toast.show{opacity:1;transform:translateX(-50%) translateY(0);}
.loader-subtitle{position:absolute;top:75px;left:30px;font-size:16px;color:rgba(var(--theme-rgb),.5);font-family:'Unbounded',sans-serif;}
.loader-image-large{position:absolute;top:120px;left:30px;width:322px;height:220px;border-radius:16px;background-image:url('https://i.pinimg.com/1200x/bd/d1/93/bdd193dd24d9d5cadd72494fd22aa8a3.jpg');background-size:cover;background-position:center;border:1px solid rgba(44,43,43,.81);display:flex;align-items:center;justify-content:center;}
.loader-stats-row{position:absolute;top:360px;left:30px;width:322px;display:flex;justify-content:space-between;font-family:'Unbounded',sans-serif;font-size:12px;color:var(--green);}
.loader-bar-bg-large{position:absolute;top:385px;left:30px;width:322px;height:16px;border-radius:8px;overflow:hidden;}
body.dark .loader-bar-bg-large{background:#212121;border:1px solid rgba(44,43,43,.81);}body.light .loader-bar-bg-large{background:#E0E0E0;border:1px solid var(--border-light);}
.loader-bar-fill-large{height:100%;width:0%;background:var(--green);border-radius:8px;transition:width .1s linear;}
)CSS";

    std::wstring css4 = LR"CSS(
.checkmark-container{display:none;width:100%;height:100%;background:rgba(0,0,0,.7);backdrop-filter:blur(4px);align-items:center;justify-content:center;border-radius:16px;}
.checkmark-svg{width:56px;height:56px;border-radius:50%;display:block;stroke-width:2;stroke:var(--green);stroke-miterlimit:10;animation:fill .4s ease-in-out .4s forwards,scale .3s ease-in-out .9s both;}
.checkmark-circle{stroke-dasharray:166;stroke-dashoffset:166;stroke-width:2;stroke-miterlimit:10;stroke:var(--green);fill:none;animation:stroke .6s cubic-bezier(.65,0,.45,1) forwards;}
.checkmark-check{transform-origin:50% 50%;stroke-dasharray:48;stroke-dashoffset:48;animation:stroke .3s cubic-bezier(.65,0,.45,1) .6s forwards;}
@keyframes stroke{100%{stroke-dashoffset:0;}}
@keyframes scale{0%,100%{transform:none;}50%{transform:scale3d(1.1,1.1,1);}}
@keyframes fill{100%{box-shadow:inset 0 0 0 30px transparent;}}
.error-log{position:absolute;bottom:80px;left:30px;width:300px;max-height:100px;overflow-y:auto;font-size:11px;color:#ff6666;font-family:monospace;background:rgba(255,0,0,.05);border:1px solid rgba(255,0,0,.2);border-radius:8px;padding:8px;display:none;word-break:break-all;box-sizing:border-box;}
.welcome-screen{position:absolute;top:0;left:0;width:var(--main-w);height:100%;display:flex;flex-direction:column;align-items:center;justify-content:center;z-index:50;transition:opacity .5s,transform .5s;}
body.dark .welcome-screen{background:var(--bg);}body.light .welcome-screen{background:var(--bg-light-card);}
.welcome-screen.hidden{opacity:0;pointer-events:none;transform:scale(.95);}
.welcome-title{font-family:'Unbounded',sans-serif;font-size:28px;color:var(--green);margin-bottom:30px;}
.welcome-subtitle{font-size:14px;margin-bottom:24px;opacity:.6;}
.welcome-options{display:flex;flex-direction:column;gap:14px;width:280px;}
.welcome-row{display:flex;justify-content:space-between;align-items:center;padding:12px 16px;border-radius:12px;transition:background .3s,border-color .3s;}
body.dark .welcome-row{background:#1a1a1a;border:1px solid var(--border);}body.light .welcome-row{background:#f0f0f0;border:1px solid var(--border-light);}
.welcome-row-label{font-size:16px;font-weight:600;}
.welcome-toggle{display:flex;gap:6px;}
.toggle-btn{padding:6px 14px;border-radius:8px;cursor:pointer;font-size:13px;font-weight:600;transition:.2s;border:1px solid transparent;display:flex;align-items:center;gap:4px;}
.toggle-btn.active{background:var(--green);color:#fff;border-color:var(--green);}
body.dark .toggle-btn:not(.active){background:#2a2a2a;color:#aaa;border-color:var(--border);}
body.light .toggle-btn:not(.active){background:#e0e0e0;color:#666;border-color:var(--border-light);}
.toggle-btn:hover:not(.active){border-color:var(--green);}
.toggle-btn svg{width:14px;height:14px;}
.welcome-continue{margin-top:20px;width:280px;height:48px;background:var(--green);border:none;border-radius:14px;font-size:18px;font-weight:600;cursor:pointer;transition:.3s;font-family:'Montserrat',sans-serif;}
body.dark .welcome-continue{color:var(--bg);}body.light .welcome-continue{color:#fff;}
.welcome-continue:hover{opacity:.9;transform:translateY(-2px);box-shadow:0 5px 15px rgba(var(--theme-rgb),.3);}
.welcome-continue:active{transform:scale(.98);}
)CSS";

    std::wstring css5 = LR"CSS(
.extra-panel{width:0;overflow:hidden;height:532px;transition:width .4s cubic-bezier(.22,1,.36,1),opacity .3s;opacity:0;flex-shrink:0;position:relative;}
.extra-panel.open{width:var(--extra-w);opacity:1;}
body.dark .extra-panel{background:var(--bg);border-top:1px solid var(--border);border-right:1px solid var(--border);border-bottom:1px solid var(--border);}
body.light .extra-panel{background:var(--bg-light-card);border-top:1px solid var(--border-light);border-right:1px solid var(--border-light);border-bottom:1px solid var(--border-light);}
.extra-panel-inner{width:var(--extra-w);padding:30px 20px;display:flex;flex-direction:column;gap:20px;}
.extra-title{font-family:'Unbounded',sans-serif;font-weight:700;font-size:20px;color:var(--green);text-transform:uppercase;letter-spacing:1px;line-height:1.2;}
.extra-divider{width:100%;height:1px;margin:4px 0;}
body.dark .extra-divider{background:var(--border);}body.light .extra-divider{background:var(--border-light);}
.extra-section{display:flex;flex-direction:column;gap:10px;}
.extra-section-label{font-size:13px;font-weight:600;color:var(--green);opacity:.7;text-transform:uppercase;letter-spacing:.5px;}
.extra-toggle-row{display:flex;gap:6px;}
</style>
)CSS";

    std::wstring svgMoon = LR"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12.79A9 9 0 1111.21 3a7 7 0 009.79 9.79z"/></svg>)";
    std::wstring svgSun = LR"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="5"/><line x1="12" y1="1" x2="12" y2="3"/><line x1="12" y1="21" x2="12" y2="23"/><line x1="4.22" y1="4.22" x2="5.64" y2="5.64"/><line x1="18.36" y1="18.36" x2="19.78" y2="19.78"/><line x1="1" y1="12" x2="3" y2="12"/><line x1="21" y1="12" x2="23" y2="12"/><line x1="4.22" y1="19.78" x2="5.64" y2="18.36"/><line x1="18.36" y1="5.64" x2="19.78" y2="4.22"/></svg>)";
    std::wstring svgChevron = LR"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><polyline points="9 18 15 12 9 6"></polyline></svg>)";

    std::wstring htmlBody = LR"HTML(
<div class="outer-container">
    <div class="wrapper">
        <div class="title-drag-area" onmousedown="window.chrome.webview.postMessage('drag_window')"></div>
        <div class="window-controls">
            <div class="dot dot-orange" onclick="window.chrome.webview.postMessage('minimize')"></div>
            <div class="dot dot-red" onclick="window.chrome.webview.postMessage('close')"></div>
        </div>
        <div id="toast" class="toast"><div class="toast-icon"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg></div><div class="toast-content"><span id="toast-title">Title</span><span id="toast-desc">Desc</span></div></div>

        <div id="welcome-screen" class="welcome-screen">
            <div class="welcome-title" id="welcomeTitle">Welcome</div>
            <div class="welcome-subtitle text-main" id="welcomeSubtitle">Choose your preferences</div>
            <div class="welcome-options">
                <div class="welcome-row">
                    <span class="welcome-row-label text-green" id="langLabel">Language</span>
                    <div class="welcome-toggle">
                        <div class="toggle-btn active" id="btnRu" onclick="setWelcomeLang('ru')">RU</div>
                        <div class="toggle-btn" id="btnEn" onclick="setWelcomeLang('en')">EN</div>
                    </div>
                </div>
                <div class="welcome-row">
                    <span class="welcome-row-label text-green" id="themeLabel">Theme</span>
                    <div class="welcome-toggle">
                        <div class="toggle-btn active" id="btnDark" onclick="setWelcomeTheme('dark')">)HTML" + svgMoon + LR"HTML(</div>
                        <div class="toggle-btn" id="btnLight" onclick="setWelcomeTheme('light')">)HTML" + svgSun + LR"HTML(</div>
                    </div>
                </div>
            </div>
            <button class="welcome-continue" id="welcomeContinueBtn" onclick="finishWelcome()">Continue</button>
        </div>

        <div id="main-screen" class="screen inactive-right">
            <div class="header-title font-unbounded text-green">
                <svg class="logo-icon" viewBox="0 0 24 24"><path d="M12 2L2 7L12 12L22 7L12 2Z"/><path d="M2 17L12 22L22 17V7L12 12L2 7V17Z"/><path d="M12 22V12"/></svg>
                <span id="cheatNameTitle">EXAMPLE</span>
            </div>
            <div class="version-row font-unbounded text-green">Minecraft __MC_VERSION__</div>
            <div class="image-frame"></div>
            <div class="description font-medium text-main" id="mainDesc">desc</div>
            <div class="credits text-faint font-medium">design by t.me/vagonsolutions.</div>
            <div class="btn-small btn-site font-semibold text-green" id="btnSiteText" onclick="window.open('https://google.com')">Site</div>
            <div class="btn-small btn-settings font-semibold text-green" id="btnSettingsText" onclick="goToSettings()">Settings</div>
            <button id="mainLaunchBtn" class="btn-launch font-semibold" onclick="handleMainButton()">Launch</button>
        </div>

        <div id="settings-screen" class="screen inactive-right">
            <div class="screen-title text-green" id="settingsTitle">Settings</div>
            <div class="nick-group">
                <label class="nick-label" id="nickLabel">Nickname</label>
                <div class="nick-row">
                    <input type="text" id="nicknameInput" class="nick-input" placeholder="Player" maxlength="16" spellcheck="false" autocomplete="off">
                    <button class="btn-nick-save" id="btnNickSave" onclick="saveNickname()">Save</button>
                </div>
            </div>
            <div class="ram-group">
                <div class="ram-header font-semibold text-green"><span id="ramLabel">RAM</span><span id="ramValue">4028MB</span></div>
                <input type="range" min="1024" max="16384" value="4028" step="128" class="slider" id="ramSlider">
            </div>
            <div class="btn-extra-settings" id="btnExtraSettings" onclick="toggleExtraPanel()">
                <span id="extraSettLabel">Advanced Settings</span>
                )HTML" + svgChevron + LR"HTML(
            </div>
            <div class="btn-back font-semibold text-green" id="btnSaveExit" onclick="saveAndExitSettings()">Save & Exit</div>
        </div>

        <div id="loading-screen" class="screen inactive-right">
            <div class="screen-title text-green" id="loadingTitle">Loading</div>
            <div class="loader-subtitle" id="loaderStatus">Downloading...</div>
            <div class="loader-image-large">
                <div class="checkmark-container" id="successCheck">
                    <svg class="checkmark-svg" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 52 52"><circle class="checkmark-circle" cx="26" cy="26" r="25" fill="none"/><path class="checkmark-check" fill="none" d="M14.1 27.2l7.1 7.2 16.7-16.8"/></svg>
                </div>
            </div>
            <div class="loader-stats-row"><span id="currentMb">0.0MB</span><span id="totalMb">...</span></div>
            <div class="loader-bar-bg-large"><div class="loader-bar-fill-large" id="loaderFill"></div></div>
            <div class="error-log" id="errorLog"></div>
            <div class="btn-cancel" id="btnCancelText" onclick="cancelInstall()">Cancel</div>
        </div>
    </div>

    <div class="extra-panel" id="extraPanel">
        <div class="extra-panel-inner">
            <div class="extra-title" id="extraPanelTitle">)HTML" + cheatNameUpper + LR"HTML(</div>
            <div class="extra-divider"></div>
            <div class="extra-section">
                <div class="extra-section-label" id="extraThemeLabel">Theme</div>
                <div class="extra-toggle-row">
                    <div class="toggle-btn" id="settBtnDark" onclick="toggleTheme('dark')">)HTML" + svgMoon + LR"HTML(</div>
                    <div class="toggle-btn" id="settBtnLight" onclick="toggleTheme('light')">)HTML" + svgSun + LR"HTML(</div>
                </div>
            </div>
            <div class="extra-section">
                <div class="extra-section-label" id="extraLangLabel">Language</div>
                <div class="extra-toggle-row">
                    <div class="toggle-btn" id="settBtnRu" onclick="toggleLang('ru')">RU</div>
                    <div class="toggle-btn" id="settBtnEn" onclick="toggleLang('en')">EN</div>
                </div>
            </div>
        </div>
    </div>
</div>
)HTML";

    std::wstring js1 = LR"JS(
<script>
const mainScreen=document.getElementById('main-screen'),settingsScreen=document.getElementById('settings-screen'),loadingScreen=document.getElementById('loading-screen'),welcomeScreen=document.getElementById('welcome-screen'),extraPanel=document.getElementById('extraPanel');
let isGameRunning=false,currentLang='ru',currentTheme='dark',currentNickname='Player',extraPanelOpen=false;
const L={ru:{welcome:'Добро пожаловать',choosePrefs:'Выберите настройки',language:'Язык',theme:'Тема',continue_:'Продолжить',settings:'Настройки',ram:'Оперативная память',saveExit:'Сохранить и выйти',site:'Сайт',launch:'Запустить',terminate:'Завершить',cancel:'Отменить',loading:'Загрузка',done:'Готово',desc:'Максимальная оптимизация, скорость и комфорт.',settingsSaved:'Конфигурация сохранена',launchedCache:'Запущен из кеша',gameTerminated:'Игра завершена',clientLaunched:'Клиент запущен',process:'Процесс',nickname:'Никнейм',save:'Сохранить',nickSaved:'Никнейм сохранён',nickEmpty:'Введите никнейм',extraSettings:'Доп. Настройки'},
en:{welcome:'Welcome',choosePrefs:'Choose your preferences',language:'Language',theme:'Theme',continue_:'Continue',settings:'Settings',ram:'RAM',saveExit:'Save & Exit',site:'Site',launch:'Launch',terminate:'Terminate',cancel:'Cancel',loading:'Loading',done:'Done',desc:'Maximum optimization, speed and comfort.',settingsSaved:'Configuration saved',launchedCache:'Launched from cache',gameTerminated:'Game terminated',clientLaunched:'Client launched',process:'Process',nickname:'Nickname',save:'Save',nickSaved:'Nickname saved',nickEmpty:'Enter a nickname',extraSettings:'Advanced Settings'}};
function t(k){return L[currentLang][k]||k;}
function refreshSlider(){const s=document.getElementById('ramSlider');updateSliderBackground(s.value,s.min,s.max);}
function applyLang(){
document.getElementById('welcomeTitle').innerText=t('welcome');document.getElementById('welcomeSubtitle').innerText=t('choosePrefs');
document.getElementById('langLabel').innerText=t('language');document.getElementById('themeLabel').innerText=t('theme');
document.getElementById('welcomeContinueBtn').innerText=t('continue_');document.getElementById('settingsTitle').innerText=t('settings');
document.getElementById('ramLabel').innerText=t('ram');document.getElementById('btnSaveExit').innerText=t('saveExit');
document.getElementById('btnSiteText').innerText=t('site');document.getElementById('btnSettingsText').innerText=t('settings');
document.getElementById('loadingTitle').innerText=t('loading');document.getElementById('btnCancelText').innerText=t('cancel');
document.getElementById('mainDesc').innerText=t('desc');
document.getElementById('nickLabel').innerText=t('nickname');
document.getElementById('btnNickSave').innerText=t('save');
document.getElementById('extraSettLabel').innerText=t('extraSettings');
document.getElementById('extraThemeLabel').innerText=t('theme');
document.getElementById('extraLangLabel').innerText=t('language');
const btn=document.getElementById('mainLaunchBtn');
if(!isGameRunning)btn.innerText=t('launch');else btn.innerText=t('terminate');}
function applyTheme(th){currentTheme=th;document.body.classList.remove('dark','light');document.body.classList.add(th);
document.getElementById('settBtnDark').classList.toggle('active',th==='dark');
document.getElementById('settBtnLight').classList.toggle('active',th==='light');refreshSlider();}
function applySettingsLang(lang){currentLang=lang;
document.getElementById('settBtnRu').classList.toggle('active',lang==='ru');
document.getElementById('settBtnEn').classList.toggle('active',lang==='en');applyLang();}
function setWelcomeLang(lang){currentLang=lang;document.getElementById('btnRu').classList.toggle('active',lang==='ru');document.getElementById('btnEn').classList.toggle('active',lang==='en');applyLang();}
function setWelcomeTheme(th){document.getElementById('btnDark').classList.toggle('active',th==='dark');document.getElementById('btnLight').classList.toggle('active',th==='light');applyTheme(th);}
function toggleTheme(th){applyTheme(th);window.chrome.webview.postMessage("set_theme:"+th);}
function toggleLang(lang){applySettingsLang(lang);window.chrome.webview.postMessage("set_lang:"+lang);}
function finishWelcome(){welcomeScreen.classList.add('hidden');mainScreen.classList.remove('inactive-right');mainScreen.classList.add('active');window.chrome.webview.postMessage("welcome_done:"+currentLang+":"+currentTheme);}
function goToSettings(){mainScreen.classList.remove('active');mainScreen.classList.add('inactive-left');settingsScreen.classList.remove('inactive-right');settingsScreen.classList.add('active');document.getElementById('nicknameInput').value=currentNickname;}
function toggleExtraPanel(){
    extraPanelOpen=!extraPanelOpen;
    const btn=document.getElementById('btnExtraSettings');
    if(extraPanelOpen){extraPanel.classList.add('open');btn.classList.add('open');window.chrome.webview.postMessage("extra_panel:open");}
    else{extraPanel.classList.remove('open');btn.classList.remove('open');window.chrome.webview.postMessage("extra_panel:close");}
}
function closeExtraPanel(){
    if(extraPanelOpen){extraPanelOpen=false;extraPanel.classList.remove('open');document.getElementById('btnExtraSettings').classList.remove('open');window.chrome.webview.postMessage("extra_panel:close");}
}
function saveNickname(){
    let nick=document.getElementById('nicknameInput').value.trim();
    nick=nick.replace(/[^A-Za-z0-9_]/g,'');
    if(nick.length===0){showToast(t('nickname'),t('nickEmpty'));return;}
    if(nick.length>16)nick=nick.substring(0,16);
    document.getElementById('nicknameInput').value=nick;
    currentNickname=nick;
    window.chrome.webview.postMessage("save_nick:"+nick);
    showToast(t('nickname'),t('nickSaved')+': '+nick);
}
function saveAndExitSettings(){closeExtraPanel();settingsScreen.classList.remove('active');settingsScreen.classList.add('inactive-right');mainScreen.classList.remove('inactive-left');mainScreen.classList.add('active');let ram=document.getElementById('ramSlider').value;window.chrome.webview.postMessage("save_ram:"+ram);showToast(t('settings'),t('settingsSaved'));}
function showToast(title,desc){const toast=document.getElementById('toast');document.getElementById('toast-title').innerText=title;document.getElementById('toast-desc').innerText=desc;toast.classList.add('show');setTimeout(()=>{toast.classList.remove('show');},3000);}
const slider=document.getElementById('ramSlider'),output=document.getElementById('ramValue');
function updateSliderBackground(v,mn,mx){const p=((v-mn)/(mx-mn))*100;const bg=currentTheme==='dark'?'#212121':'#E0E0E0';slider.style.background='linear-gradient(to right, var(--green) '+p+'%, '+bg+' '+p+'%)';}
slider.addEventListener('input',function(){output.innerHTML=this.value+"MB";updateSliderBackground(this.value,this.min,this.max);});
function handleMainButton(){window.chrome.webview.postMessage("action_button");}
function cancelInstall(){window.chrome.webview.postMessage("cancel_install");loadingScreen.classList.remove('active');loadingScreen.classList.add('inactive-right');mainScreen.classList.remove('inactive-left');mainScreen.classList.add('active');}
function setRunningState(r){isGameRunning=r;const btn=document.getElementById('mainLaunchBtn');if(r){btn.innerText=t('terminate');btn.classList.add('btn-quit-mode');}else{btn.innerText=t('launch');btn.classList.remove('btn-quit-mode');}}
)JS";

    std::wstring js2 = LR"JS(
function startLoadingUI(){closeExtraPanel();mainScreen.classList.remove('active');mainScreen.classList.add('inactive-left');loadingScreen.classList.remove('inactive-right');loadingScreen.classList.add('active');document.getElementById('successCheck').style.display='none';document.getElementById('loaderFill').style.width='0%';document.getElementById('errorLog').style.display='none';document.getElementById('errorLog').innerText='';}
function updateProgress(p,c,tot,s){document.getElementById('loaderFill').style.width=p+'%';document.getElementById('currentMb').innerText=c;document.getElementById('totalMb').innerText=tot;document.getElementById('loaderStatus').innerText=s;}
function showError(m){const el=document.getElementById('errorLog');el.style.display='block';el.innerText+=m+'\n';el.scrollTop=el.scrollHeight;}
function finishLoading(){document.getElementById('loaderStatus').innerText=t('done');document.getElementById('loaderStatus').style.color='var(--green)';document.getElementById('successCheck').style.display='flex';setTimeout(()=>{loadingScreen.classList.remove('active');loadingScreen.classList.add('inactive-right');mainScreen.classList.remove('inactive-left');mainScreen.classList.add('active');setRunningState(true);showToast(t('done'),t('clientLaunched'));},2500);}
function skipWelcome(lang,theme,nick){currentLang=lang;currentNickname=nick||'Player';applyTheme(theme);applySettingsLang(lang);applyLang();welcomeScreen.classList.add('hidden');mainScreen.classList.remove('inactive-right');mainScreen.classList.add('active');}
window.chrome.webview.addEventListener('message',event=>{const msg=event.data;
if(msg.type==='progress'){updateProgress(msg.percent,msg.current,msg.total,msg.status);}
else if(msg.type==='finish_install'){finishLoading();}
else if(msg.type==='set_ram'){slider.value=msg.value;output.innerText=msg.value+"MB";updateSliderBackground(slider.value,slider.min,slider.max);}
else if(msg.type==='launch_success'){setRunningState(true);showToast(t('process'),t('launchedCache'));}
else if(msg.type==='start_load'){startLoadingUI();}
else if(msg.type==='install_canceled'){setRunningState(false);showToast(t('loading'),t('cancel'));}
else if(msg.type==='process_stopped'){setRunningState(false);showToast(t('process'),t('gameTerminated'));}
else if(msg.type==='error'){showError(msg.message);}
else if(msg.type==='init_settings'){skipWelcome(msg.lang,msg.theme,msg.nickname);}
else if(msg.type==='set_nickname'){currentNickname=msg.value;document.getElementById('nicknameInput').value=msg.value;}
});
applyLang();applyTheme('dark');
</script>
)JS";

    std::wstring html =
        L"<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\">"
        L"<link href=\"https://fonts.googleapis.com/css2?family=Montserrat:wght@500;600;700&family=Unbounded:wght@400;500;700&display=swap\" rel=\"stylesheet\">" +
        css1 + css2 + css3 + css4 + css5 + L"</head><body class=\"dark\">" + htmlBody + js1 + js2 + L"</body></html>";

    std::wstring ph = L"EXAMPLE";
    size_t pos = 0;
    while ((pos = html.find(ph, pos)) != std::wstring::npos) {
        html.replace(pos, ph.length(), cheatNameUpper);
        pos += cheatNameUpper.length();
    }

    std::wstring versionPlaceholder = L"__MC_VERSION__";
    pos = 0;
    while ((pos = html.find(versionPlaceholder, pos)) != std::wstring::npos) {
        html.replace(pos, versionPlaceholder.length(), MC_VERSION);
        pos += MC_VERSION.length();
    }

    WNDCLASSEXW wcex = { sizeof(WNDCLASSEX) }; wcex.style = CS_HREDRAW | CS_VREDRAW; wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance; wcex.hCursor = LoadCursor(nullptr, IDC_ARROW); wcex.lpszClassName = L"LauncherClass";
    RegisterClassExW(&wcex);

    int sW = GetSystemMetrics(SM_CXSCREEN), sH = GetSystemMetrics(SM_CYSCREEN);
    g_hWnd = CreateWindowExW(WS_EX_LAYERED, L"LauncherClass", CHEAT_NAME.c_str(), WS_POPUP | WS_VISIBLE,
        (sW - MAIN_WIDTH) / 2, (sH - MAIN_HEIGHT) / 2, MAIN_WIDTH, MAIN_HEIGHT, nullptr, nullptr, hInstance, nullptr);
    if (!g_hWnd) return -1;
    DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(g_hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
    SetLayeredWindowAttributes(g_hWnd, 0, 255, LWA_ALPHA);

    HRESULT webViewInitHr = CreateCoreWebView2EnvironmentWithOptions(nullptr, (GetBaseDir() + L"cache").c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [html, saved](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || env == nullptr) {
                    SendError(L"WebView2 environment init failed");
                    return E_FAIL;
                }

                return env->CreateCoreWebView2Controller(g_hWnd, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [html, saved](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                        if (FAILED(result) || controller == nullptr) {
                            SendError(L"WebView2 controller init failed");
                            return E_FAIL;
                        }

                        g_webviewController = controller;
                        if (FAILED(g_webviewController->get_CoreWebView2(&g_webview)) || !g_webview) {
                            SendError(L"WebView2 core init failed");
                            return E_FAIL;
                        }

                        wil::com_ptr<ICoreWebView2Settings> settings;
                        if (FAILED(g_webview->get_Settings(&settings)) || !settings) {
                            SendError(L"WebView2 settings init failed");
                            return E_FAIL;
                        }

                        settings->put_AreDefaultContextMenusEnabled(FALSE);
                        settings->put_AreDevToolsEnabled(FALSE);
                        RECT b; GetClientRect(g_hWnd, &b); g_webviewController->put_Bounds(b);
                        g_webview->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                            [](ICoreWebView2* wv, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                LPWSTR pw = nullptr;
                                if (FAILED(args->TryGetWebMessageAsString(&pw)) || pw == nullptr) return S_OK;

                                std::wstring msg(pw);
                                CoTaskMemFree(pw);
                                if (msg == L"close") DestroyWindow(g_hWnd);
                                else if (msg == L"minimize") ShowWindow(g_hWnd, SW_MINIMIZE);
                                else if (msg == L"drag_window") { ReleaseCapture(); SendMessage(g_hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0); }
                                else if (msg == L"action_button") StartProcessLogic();
                                else if (msg == L"cancel_install") g_CancelDownload = true;
                                else if (msg == L"extra_panel:open") ResizeWindow(true);
                                else if (msg == L"extra_panel:close") ResizeWindow(false);
                                else if (msg.find(L"save_nick:") == 0) {
                                    std::wstring nick = msg.substr(10); std::wstring safe;
                                    for (wchar_t c : nick) {
                                        if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'_') safe += c;
                                    }
                                    if (safe.empty()) safe = L"Player"; if (safe.length() > 16) safe = safe.substr(0, 16);
                                    g_Nickname = safe;
                                    SaveRegistry(LoadRegistry().isInstalled, g_RamAmount, g_DarkTheme, g_LangRu, g_Nickname);
                                    std::string nickUtf8 = SanitizeNicknameForJson(safe);
                                    std::wstring js = L"{ \"type\": \"set_nickname\", \"value\": \"" + Utf8ToWide(nickUtf8) + L"\" }";
                                    g_webview->PostWebMessageAsJson(js.c_str());
                                }
                                else if (msg.find(L"save_ram:") == 0) {
                                    int requested = g_RamAmount;
                                    try {
                                        requested = std::stoi(msg.substr(9));
                                    }
                                    catch (...) {
                                        SendError(L"Invalid RAM value");
                                        return S_OK;
                                    }
                                    int safe = GetSafeRamAmount(requested);
                                    g_RamAmount = safe;
                                    SaveRegistry(LoadRegistry().isInstalled, g_RamAmount, g_DarkTheme, g_LangRu, g_Nickname);
                                    if (safe != requested) {
                                        std::wstring js = L"{ \"type\": \"set_ram\", \"value\": " + std::to_wstring(safe) + L" }";
                                        g_webview->PostWebMessageAsJson(js.c_str());
                                    }
                                }
                                else if (msg.find(L"set_theme:") == 0) { g_DarkTheme = (msg.substr(10) == L"dark"); SaveRegistry(LoadRegistry().isInstalled, g_RamAmount, g_DarkTheme, g_LangRu, g_Nickname); }
                                else if (msg.find(L"set_lang:") == 0) { g_LangRu = (msg.substr(9) == L"ru"); SaveRegistry(LoadRegistry().isInstalled, g_RamAmount, g_DarkTheme, g_LangRu, g_Nickname); }
                                else if (msg.find(L"welcome_done:") == 0) {
                                    std::wstring p = msg.substr(13); size_t s = p.find(L':');
                                    if (s != std::wstring::npos) { g_LangRu = (p.substr(0, s) == L"ru"); g_DarkTheme = (p.substr(s + 1) == L"dark"); }
                                    SaveRegistry(LoadRegistry().isInstalled, g_RamAmount, g_DarkTheme, g_LangRu, g_Nickname);
                                }
                                return S_OK;
                            }).Get(), nullptr);
                        g_webview->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
                            [saved](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                                std::wstring js = L"{ \"type\": \"set_ram\", \"value\": " + std::to_wstring(saved.ram) + L" }";
                                g_webview->PostWebMessageAsJson(js.c_str());
                                if (saved.hasPrefs) {
                                    std::wstring ls = saved.langRu ? L"ru" : L"en", ts = saved.darkTheme ? L"dark" : L"light";
                                    std::string nickJson = SanitizeNicknameForJson(saved.nickname);
                                    std::wstring initJs = L"{ \"type\": \"init_settings\", \"lang\": \"" + ls + L"\", \"theme\": \"" + ts + L"\", \"nickname\": \"" + Utf8ToWide(nickJson) + L"\" }";
                                    g_webview->PostWebMessageAsJson(initJs.c_str());
                                }
                                return S_OK;
                            }).Get(), nullptr);
                        g_webview->NavigateToString(html.c_str());
                        return S_OK;
                    }).Get());
            }).Get());
    if (FAILED(webViewInitHr)) {
        SendError(L"CreateCoreWebView2EnvironmentWithOptions failed");
    }

    MSG msg; while (GetMessage(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return (int)msg.wParam;
}
