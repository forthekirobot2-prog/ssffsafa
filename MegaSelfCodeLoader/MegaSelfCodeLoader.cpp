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


std::wstring CHEAT_NAME = L"Xlority Client";
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

const int MAIN_WIDTH = 980;
const int MAIN_HEIGHT = 520;
const int EXTRA_WIDTH = 0;

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
    case WM_NCHITTEST: {
        POINT p = { LOWORD(lParam), HIWORD(lParam) };
        ScreenToClient(hWnd, &p);
        if (p.y < 44 && p.x < 560) return HTCAPTION;
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    case WM_DESTROY: g_CancelDownload = true; TerminateGame(); PostQuitMessage(0); break;
    default: return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    RegState saved = LoadRegistry();
    g_DarkTheme = true; g_LangRu = saved.langRu; g_RamAmount = saved.ram;
    g_HasSavedPrefs = saved.hasPrefs; g_Nickname = saved.nickname;

    std::wstring cheatNameDisplay = CHEAT_NAME;

    std::wstring css1 = LR"CSS(
<style>
:root{
  --bg:#000000;
  --panel:#0A0A0A;
  --panel-soft:#111111;
  --line:#262626;
  --text:#FFFFFF;
  --muted:#CFCFCF;
}
*{box-sizing:border-box;}
body{
  margin:0;
  padding:0;
  background:#000;
  color:var(--text);
  font-family:'Segoe UI Variable Text','Segoe UI',Tahoma,sans-serif;
  overflow:hidden;
  user-select:none;
  -webkit-font-smoothing:antialiased;
  -moz-osx-font-smoothing:grayscale;
  text-rendering:optimizeLegibility;
}
.app{
  position:relative;
  width:980px;
  height:520px;
  border-radius:20px;
  border:1px solid var(--line);
  background:linear-gradient(180deg,#070707 0%,#050505 100%);
  box-shadow:0 34px 90px rgba(0,0,0,.7);
  overflow:hidden;
}
.drag-zone{position:absolute;top:0;left:0;right:130px;height:44px;z-index:20;}
.window-controls{position:absolute;top:8px;right:10px;display:flex;gap:8px;z-index:40;}
.win-btn{
  width:34px;
  height:28px;
  border-radius:9px;
  border:1px solid #2E2E2E;
  background:#111;
  color:#EDEDED;
  font-size:15px;
  font-weight:400;
  display:flex;
  align-items:center;
  justify-content:center;
  cursor:pointer;
  transition:.2s;
}
.win-btn:hover{background:#171717;border-color:#3A3A3A;}
.main{padding:54px 24px 18px;height:100%;display:flex;flex-direction:column;gap:12px;}
.top{display:flex;justify-content:space-between;align-items:flex-end;gap:14px;padding-bottom:10px;border-bottom:1px solid #1F1F1F;}
.brand{display:flex;align-items:center;gap:10px;min-width:0;flex:1;}
.logo{width:30px;height:30px;border-radius:9px;border:1px solid #353535;background:#101010;display:flex;align-items:center;justify-content:center;flex-shrink:0;}
.logo svg{width:18px;height:18px;fill:#FFF;}
.title{font-size:38px;line-height:1.02;font-weight:400;letter-spacing:.15px;white-space:nowrap;}
.top-tabs{display:flex;align-items:flex-end;gap:8px;}
.tab{
  height:42px;
  border-radius:10px 10px 0 0;
  border:1px solid #333;
  border-bottom-color:#1F1F1F;
  background:#0D0D0D;
  color:#F1F1F1;
  padding:0 16px;
  display:flex;
  align-items:center;
  font-weight:400;
  line-height:1;
}
.tab-version{font-size:24px;cursor:default;white-space:nowrap;}
.tab-settings{font-size:18px;cursor:pointer;transition:.2s;}
.tab-settings:hover{background:#151515;border-color:#474747;}
.tab-settings.active{background:#FFF;color:#000;border-color:#FFF;border-bottom-color:#FFF;}
.desc{font-size:22px;line-height:1.32;font-weight:400;max-width:930px;color:#FAFAFA;padding:10px 0 8px;}
.actions{display:flex;align-items:center;}
.btn{
  height:52px;
  border-radius:12px;
  border:1px solid #343434;
  background:#0E0E0E;
  color:#FFF;
  font-size:20px;
  font-weight:400;
  padding:0 20px;
  cursor:pointer;
  transition:.2s;
}
.btn:hover{background:#141414;border-color:#444;}
.btn.launch{width:100%;font-size:28px;height:62px;background:#FFF;color:#000;border-color:#FFF;}
.btn.launch:hover{filter:brightness(.95);}
.btn.launch.terminate{background:#171717;color:#FFF;border-color:#464646;}
.settings-panel{
  border-radius:14px;
  border:1px solid #333;
  background:#0D0D0D;
  padding:12px;
  display:none;
  gap:10px;
}
.settings-panel.open{display:grid;grid-template-columns:1.25fr 1fr auto;grid-template-areas:"nick lang save" "ram ram ram";align-items:end;column-gap:10px;row-gap:10px;}
.field{display:flex;flex-direction:column;gap:7px;min-width:0;}
.field-nick{grid-area:nick;}
.field-lang{grid-area:lang;}
.field-ram{grid-area:ram;}
.field-save{grid-area:save;}
.label{font-size:13px;font-weight:400;color:var(--muted);}
.input{
  height:42px;
  border-radius:10px;
  border:1px solid #373737;
  background:#060606;
  color:#FFF;
  padding:0 12px;
  font-size:16px;
  font-weight:400;
  outline:none;
}
.input:focus{border-color:#4A4A4A;}
.ram{display:flex;justify-content:space-between;align-items:center;font-size:12px;font-weight:400;color:#D7D7D7;margin-bottom:7px;}
.slider{width:100%;accent-color:#FFFFFF;height:10px;}
.langs{display:flex;gap:8px;}
.lang-btn{
  flex:1;
  height:42px;
  border-radius:10px;
  border:1px solid #373737;
  background:#0B0B0B;
  color:#EAEAEA;
  font-size:15px;
  font-weight:400;
  cursor:pointer;
}
.lang-btn.active{background:#FFF;color:#000;border-color:#FFF;}
.btn.save{height:42px;font-size:16px;min-width:120px;background:#FFF;color:#000;border-color:#FFF;}
.loader{
  border-radius:14px;
  border:1px solid #333;
  background:#0B0B0B;
  padding:12px;
  display:none;
  gap:8px;
}
.loader.show{display:flex;flex-direction:column;}
.loader-top{display:flex;justify-content:space-between;align-items:center;font-size:14px;font-weight:400;color:#F0F0F0;}
.loader-sub{font-size:12px;color:#C9C9C9;font-weight:400;}
.progress-track{height:14px;border-radius:999px;border:1px solid #3A3A3A;background:#050505;overflow:hidden;}
.progress-fill{height:100%;width:0%;background:#FFFFFF;transition:width .18s linear;}
.error-log{margin:0;max-height:90px;overflow:auto;border-radius:10px;border:1px solid #2E2E2E;background:#070707;color:#E6E6E6;padding:9px;font-size:11px;font-family:Consolas,monospace;display:none;white-space:pre-wrap;}
.loader-actions{display:flex;justify-content:flex-end;}
.btn.cancel{height:40px;font-size:15px;padding:0 16px;}
.toast{
  position:absolute;
  right:22px;
  bottom:18px;
  max-width:420px;
  padding:11px 13px;
  border-radius:12px;
  border:1px solid #3A3A3A;
  background:#111;
  box-shadow:0 14px 28px rgba(0,0,0,.42);
  opacity:0;
  transform:translateY(18px);
  pointer-events:none;
  transition:.25s;
}
.toast.show{opacity:1;transform:translateY(0);}
.toast strong{display:block;font-size:14px;font-weight:400;margin-bottom:2px;color:#FFF;}
.toast span{font-size:13px;font-weight:400;color:#DCDCDC;}
</style>
)CSS";

    std::wstring htmlBody = LR"HTML(
<div class="app">
  <div class="drag-zone"></div>
  <div class="window-controls">
    <div class="win-btn" onclick="window.chrome.webview.postMessage('minimize')">&#8211;</div>
    <div class="win-btn" onclick="window.chrome.webview.postMessage('close')">&#10005;</div>
  </div>

  <main class="main">
    <div class="top">
      <div class="brand">
        <div class="logo"><svg viewBox="0 0 24 24"><path d="M12 2.7 4.4 7.1v9.8l7.6 4.4 7.6-4.4V7.1z"/><path d="M8.2 8.8 12 11.3l3.8-2.5" fill="none" stroke="#090909" stroke-width="1.8" stroke-linecap="round"/><path d="M8.2 15.2 12 12.7l3.8 2.5" fill="none" stroke="#090909" stroke-width="1.8" stroke-linecap="round"/></svg></div>
        <div class="title">__CHEAT_NAME__</div>
      </div>
      <div class="top-tabs">
        <div class="tab tab-version">Minecraft __MC_VERSION__</div>
        <button class="tab tab-settings" id="settingsTabBtn" onclick="toggleSettings()">Настройки</button>
      </div>
    </div>

    <div class="desc" id="mainDesc">Xlority — Лучший бесплатный чит клиент на Minecraft, с самым большим функционалом и лучшими обходами.</div>

    <div class="actions">
      <button class="btn launch" id="launchBtn" onclick="handleLaunchClick()">Запустить</button>
    </div>

    <div class="settings-panel" id="settingsPanel">
      <div class="field field-nick">
        <label class="label" id="nickLabel">Никнейм</label>
        <input class="input" id="nicknameInput" maxlength="16" spellcheck="false" autocomplete="off" placeholder="Player" />
      </div>

      <div class="field field-ram">
        <div class="ram"><span id="ramLabel">Оперативная память</span><span id="ramValue">4028 MB</span></div>
        <input type="range" min="1024" max="16384" step="128" value="4028" class="slider" id="ramSlider" />
      </div>

      <div class="field field-lang">
        <label class="label" id="langLabel">Язык</label>
        <div class="langs">
          <button class="lang-btn active" id="langRuBtn" onclick="switchLang('ru')">RU</button>
          <button class="lang-btn" id="langEnBtn" onclick="switchLang('en')">EN</button>
        </div>
      </div>

      <button class="btn save field-save" id="saveBtn" onclick="saveSettings()">Сохранить</button>
    </div>

    <div class="loader" id="loaderPanel">
      <div class="loader-top">
        <span id="loaderStatus">Готово к запуску</span>
        <span class="loader-sub"><span id="currentMb">0.0MB</span> / <span id="totalMb">...</span></span>
      </div>
      <div class="progress-track"><div class="progress-fill" id="loaderFill"></div></div>
      <pre class="error-log" id="errorLog"></pre>
      <div class="loader-actions"><button class="btn cancel" id="cancelBtn" onclick="cancelInstall()">Отменить</button></div>
    </div>
  </main>

  <div class="toast" id="toast"><strong id="toastTitle">title</strong><span id="toastText">text</span></div>
</div>
)HTML";

    std::wstring js1 = LR"JS(
<script>
const settingsPanel=document.getElementById('settingsPanel');
const loaderPanel=document.getElementById('loaderPanel');
const launchBtn=document.getElementById('launchBtn');
const settingsTabBtn=document.getElementById('settingsTabBtn');
const nicknameInput=document.getElementById('nicknameInput');
const ramSlider=document.getElementById('ramSlider');
const ramValue=document.getElementById('ramValue');

let currentLang='ru';
let currentNickname='Player';
let isGameRunning=false;

const L={
ru:{
  desc:'Xlority — Лучший бесплатный чит клиент на Minecraft, с самым большим функционалом и лучшими обходами.',
  settings:'Настройки',settingsTab:'Настройки',launch:'Запустить',terminate:'Завершить',
  nick:'Никнейм',ram:'Оперативная память',lang:'Язык',save:'Сохранить',
  ready:'Готово к запуску',cancel:'Отменить',done:'Готово',
  process:'Клиент',cache:'Запущено из кеша',stopped:'Игра завершена',started:'Клиент запущен',saved:'Настройки сохранены',nickEmpty:'Введите никнейм'
},
en:{
  desc:'Xlority — The best free Minecraft cheat client, with the biggest feature set and strongest bypasses.',
  settings:'Settings',settingsTab:'Settings',launch:'Launch',terminate:'Terminate',
  nick:'Nickname',ram:'RAM',lang:'Language',save:'Save',
  ready:'Ready to launch',cancel:'Cancel',done:'Done',
  process:'Client',cache:'Launched from cache',stopped:'Game terminated',started:'Client launched',saved:'Settings saved',nickEmpty:'Enter a nickname'
}};

function t(k){return (L[currentLang]&&L[currentLang][k])?L[currentLang][k]:k;}

function applyLang(){
  document.getElementById('mainDesc').innerText=t('desc');
  document.getElementById('nickLabel').innerText=t('nick');
  document.getElementById('ramLabel').innerText=t('ram');
  document.getElementById('langLabel').innerText=t('lang');
  document.getElementById('saveBtn').innerText=t('save');
  document.getElementById('cancelBtn').innerText=t('cancel');
  settingsTabBtn.innerText=t('settingsTab');
  launchBtn.innerText=isGameRunning?t('terminate'):t('launch');
  if(document.getElementById('loaderStatus').innerText.trim()==='')document.getElementById('loaderStatus').innerText=t('ready');
  syncSettingsTab();
}

function showToast(title,text){
  const toast=document.getElementById('toast');
  document.getElementById('toastTitle').innerText=title;
  document.getElementById('toastText').innerText=text;
  toast.classList.add('show');
  setTimeout(()=>toast.classList.remove('show'),2600);
}

function cleanNick(v){
  let n=(v||'').trim().replace(/[^A-Za-z0-9_]/g,'');
  if(n.length>16)n=n.substring(0,16);
  return n;
}

function setRunningState(v){
  isGameRunning=v;
  launchBtn.classList.toggle('terminate',v);
  launchBtn.innerText=v?t('terminate'):t('launch');
}

function toggleSettings(){
  settingsPanel.classList.toggle('open');
  syncSettingsTab();
}

function handleLaunchClick(){window.chrome.webview.postMessage('action_button');}
function cancelInstall(){window.chrome.webview.postMessage('cancel_install');}

function switchLang(lang){
  currentLang=(lang==='en')?'en':'ru';
  document.getElementById('langRuBtn').classList.toggle('active',currentLang==='ru');
  document.getElementById('langEnBtn').classList.toggle('active',currentLang==='en');
  applyLang();
  window.chrome.webview.postMessage('set_lang:'+currentLang);
}

function saveSettings(){
  let nick=cleanNick(nicknameInput.value);
  if(nick.length===0){showToast(t('nick'),t('nickEmpty'));return;}
  nicknameInput.value=nick;
  currentNickname=nick;
  window.chrome.webview.postMessage('save_nick:'+nick);

  const ram=Number(ramSlider.value)||4028;
  window.chrome.webview.postMessage('save_ram:'+ram);

  showToast(t('settings'),t('saved'));
  settingsPanel.classList.remove('open');
  syncSettingsTab();
}

function syncSettingsTab(){
  settingsTabBtn.classList.toggle('active',settingsPanel.classList.contains('open'));
}

ramSlider.addEventListener('input',()=>{ramValue.innerText=ramSlider.value+' MB';});
)JS";

    std::wstring js2 = LR"JS(
window.chrome.webview.addEventListener('message',(event)=>{
  const msg=event.data;
  if(msg.type==='progress'){
    loaderPanel.classList.add('show');
    const p=Math.max(0,Math.min(100,Number(msg.percent)||0));
    document.getElementById('loaderFill').style.width=p+'%';
    document.getElementById('currentMb').innerText=msg.current||'0.0MB';
    document.getElementById('totalMb').innerText=msg.total||'...';
    document.getElementById('loaderStatus').innerText=msg.status||t('ready');
  } else if(msg.type==='finish_install'){
    document.getElementById('loaderStatus').innerText=t('done');
    document.getElementById('loaderFill').style.width='100%';
    setTimeout(()=>{loaderPanel.classList.remove('show');setRunningState(true);showToast(t('done'),t('started'));},1200);
  } else if(msg.type==='set_ram'){
    const v=Number(msg.value)||4028;
    ramSlider.value=v;
    ramValue.innerText=v+' MB';
  } else if(msg.type==='launch_success'){
    setRunningState(true);
    showToast(t('process'),t('cache'));
  } else if(msg.type==='start_load'){
    loaderPanel.classList.add('show');
    document.getElementById('loaderStatus').innerText=t('ready');
    document.getElementById('loaderFill').style.width='0%';
    const err=document.getElementById('errorLog');
    err.style.display='none';
    err.innerText='';
  } else if(msg.type==='install_canceled'){
    loaderPanel.classList.remove('show');
    setRunningState(false);
    showToast(t('process'),t('cancel'));
  } else if(msg.type==='process_stopped'){
    setRunningState(false);
    showToast(t('process'),t('stopped'));
  } else if(msg.type==='error'){
    const err=document.getElementById('errorLog');
    err.style.display='block';
    err.innerText+=(msg.message||'')+'\n';
    err.scrollTop=err.scrollHeight;
  } else if(msg.type==='init_settings'){
    if(msg.lang==='en'||msg.lang==='ru')currentLang=msg.lang;
    if(typeof msg.nickname==='string'&&msg.nickname.length>0){currentNickname=msg.nickname;nicknameInput.value=msg.nickname;}
    document.getElementById('langRuBtn').classList.toggle('active',currentLang==='ru');
    document.getElementById('langEnBtn').classList.toggle('active',currentLang==='en');
    applyLang();
  } else if(msg.type==='set_nickname'){
    if(typeof msg.value==='string'&&msg.value.length>0){currentNickname=msg.value;nicknameInput.value=msg.value;}
  }
});

ramValue.innerText=ramSlider.value+' MB';
applyLang();
</script>
)JS";

    std::wstring html =
        L"<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\">" +
        css1 + L"</head><body>" + htmlBody + js1 + js2 + L"</body></html>";

    std::wstring namePlaceholder = L"__CHEAT_NAME__";
    size_t pos = 0;
    while ((pos = html.find(namePlaceholder, pos)) != std::wstring::npos) {
        html.replace(pos, namePlaceholder.length(), cheatNameDisplay);
        pos += cheatNameDisplay.length();
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
                                    std::wstring ls = saved.langRu ? L"ru" : L"en", ts = L"dark";
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


