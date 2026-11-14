#include "MemoryManager.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
static constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
static constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace {

struct HttpResponse {
    int status = 200;
    std::string contentType = "application/json; charset=utf-8";
    std::string body;
};

std::string statusText(int status) {
    switch (status) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 500: return "Internal Server Error";
    default: return "Error";
    }
}

std::string jsonEscape(const std::string& value) {
    std::ostringstream escaped;
    for (const unsigned char character : value) {
        switch (character) {
        case '"': escaped << "\\\""; break;
        case '\\': escaped << "\\\\"; break;
        case '\b': escaped << "\\b"; break;
        case '\f': escaped << "\\f"; break;
        case '\n': escaped << "\\n"; break;
        case '\r': escaped << "\\r"; break;
        case '\t': escaped << "\\t"; break;
        default:
            if (character < 0x20) {
                escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(character) << std::dec;
            } else {
                escaped << static_cast<char>(character);
            }
        }
    }
    return escaped.str();
}

std::string optionalNumber(const std::optional<std::size_t>& value, bool oneBased = false) {
    if (!value) return "null";
    return std::to_string(*value + (oneBased ? 1 : 0));
}

std::string serializePages(const std::vector<std::optional<int>>& pages) {
    std::ostringstream output;
    output << '[';
    for (std::size_t i = 0; i < pages.size(); ++i) {
        if (i) output << ',';
        if (pages[i]) output << *pages[i];
        else output << "null";
    }
    output << ']';
    return output.str();
}

std::string serializeResult(const paging::SimulationResult& result) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(2);
    output << "{\"algorithm\":\"" << paging::algorithmName(result.algorithm)
           << "\",\"frameCapacity\":" << result.frameCapacity
           << ",\"swapCapacity\":" << result.swapCapacity
           << ",\"completed\":" << (result.completed ? "true" : "false")
           << ",\"error\":\"" << jsonEscape(result.error) << "\",\"stats\":{"
           << "\"requests\":" << result.requests
           << ",\"hits\":" << result.hits
           << ",\"faults\":" << result.faults
           << ",\"swapIns\":" << result.swapIns
           << ",\"swapOuts\":" << result.swapOuts
           << ",\"hitRate\":" << result.hitRate()
           << ",\"faultRate\":" << result.faultRate()
           << "},\"steps\":[";
    for (std::size_t i = 0; i < result.steps.size(); ++i) {
        const auto& step = result.steps[i];
        if (i) output << ',';
        output << "{\"step\":" << step.step
               << ",\"page\":" << step.page
               << ",\"result\":\"" << jsonEscape(step.result)
               << "\",\"action\":\"" << jsonEscape(step.action)
               << "\",\"evictedPage\":";
        if (step.evictedPage) output << *step.evictedPage;
        else output << "null";
        output << ",\"swappedIn\":" << (step.swappedIn ? "true" : "false")
               << ",\"frame\":" << optionalNumber(step.frame, true)
               << ",\"frames\":" << serializePages(step.frames)
               << ",\"swap\":" << serializePages(step.swapSlots)
               << ",\"pageTable\":[";
        for (std::size_t row = 0; row < step.pageTable.size(); ++row) {
            if (row) output << ',';
            const auto& entry = step.pageTable[row];
            output << "{\"page\":" << entry.page
                   << ",\"present\":" << (entry.entry.present ? "true" : "false")
                   << ",\"frame\":" << optionalNumber(entry.entry.frame, true)
                   << ",\"inSwap\":" << (entry.entry.inSwap ? "true" : "false") << '}';
        }
        output << "]}";
    }
    output << "]}";
    return output.str();
}

std::string urlDecode(const std::string& value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            decoded.push_back(' ');
        } else if (value[i] == '%' && i + 2 < value.size()) {
            const auto hexValue = [](char character) -> int {
                if (character >= '0' && character <= '9') return character - '0';
                if (character >= 'a' && character <= 'f') return character - 'a' + 10;
                if (character >= 'A' && character <= 'F') return character - 'A' + 10;
                return -1;
            };
            const int high = hexValue(value[i + 1]);
            const int low = hexValue(value[i + 2]);
            if (high < 0 || low < 0) throw std::invalid_argument("Malformed URL encoding.");
            decoded.push_back(static_cast<char>(high * 16 + low));
            i += 2;
        } else if (value[i] == '%') {
            throw std::invalid_argument("Malformed URL encoding.");
        } else {
            decoded.push_back(value[i]);
        }
    }
    return decoded;
}

std::map<std::string, std::string> parseQuery(const std::string& query) {
    std::map<std::string, std::string> values;
    std::size_t start = 0;
    while (start <= query.size()) {
        const std::size_t end = query.find('&', start);
        const std::string pair = query.substr(start, end == std::string::npos ? end : end - start);
        if (!pair.empty()) {
            const std::size_t equals = pair.find('=');
            const std::string key = urlDecode(pair.substr(0, equals));
            const std::string value = equals == std::string::npos ? "" : urlDecode(pair.substr(equals + 1));
            if (!values.emplace(key, value).second) {
                throw std::invalid_argument("Duplicate query parameter: " + key);
            }
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return values;
}

std::size_t parseSize(const std::map<std::string, std::string>& query,
                      const std::string& key, std::size_t minimum, std::size_t maximum) {
    const auto found = query.find(key);
    if (found == query.end() || found->second.empty()) {
        throw std::invalid_argument("Missing required query parameter: " + key);
    }
    std::size_t parsed = 0;
    try {
        std::size_t consumed = 0;
        parsed = std::stoull(found->second, &consumed);
        if (consumed != found->second.size()) throw std::invalid_argument("");
    } catch (const std::exception&) {
        throw std::invalid_argument("Parameter '" + key + "' must be a whole number.");
    }
    if (parsed < minimum || parsed > maximum) {
        throw std::invalid_argument("Parameter '" + key + "' must be between " +
                                    std::to_string(minimum) + " and " + std::to_string(maximum) + ".");
    }
    return parsed;
}

std::vector<int> parseReferences(const std::string& value) {
    std::string normalized = value;
    std::replace(normalized.begin(), normalized.end(), ',', ' ');
    std::istringstream input(normalized);
    std::vector<int> references;
    std::string token;
    while (input >> token) {
        if (token.empty() || !std::all_of(token.begin(), token.end(), [](unsigned char ch) {
                return std::isdigit(ch) != 0;
            })) {
            throw std::invalid_argument("References must be whole page numbers from 0 to 99.");
        }
        try {
            const long page = std::stol(token);
            if (page > 99) throw std::invalid_argument("");
            references.push_back(static_cast<int>(page));
        } catch (const std::exception&) {
            throw std::invalid_argument("References must be whole page numbers from 0 to 99.");
        }
        if (references.size() > 100) {
            throw std::invalid_argument("Use no more than 100 page references.");
        }
    }
    if (references.empty()) throw std::invalid_argument("Enter at least one page reference.");
    return references;
}

std::string mimeType(const std::filesystem::path& path) {
    if (path.extension() == ".html") return "text/html; charset=utf-8";
    if (path.extension() == ".css") return "text/css; charset=utf-8";
    if (path.extension() == ".js") return "application/javascript; charset=utf-8";
    return "application/octet-stream";
}

HttpResponse fileResponse(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {404, "application/json; charset=utf-8", "{\"error\":\"Frontend file not found. Run the server from the project root.\"}"};
    std::ostringstream contents;
    contents << input.rdbuf();
    return {200, mimeType(path), contents.str()};
}

HttpResponse handleRequest(const std::string& method, const std::string& target) {
    if (method == "OPTIONS") return {204, "application/json; charset=utf-8", ""};
    if (method != "GET") return {405, "application/json; charset=utf-8", "{\"error\":\"Only GET requests are supported.\"}"};

    const std::size_t question = target.find('?');
    const std::string path = target.substr(0, question);
    if (path == "/" || path == "/index.html") return fileResponse("frontend/index.html");
    if (path == "/style.css") return fileResponse("frontend/style.css");
    if (path == "/script.js") return fileResponse("frontend/script.js");
    if (path == "/api/health") return {200, "application/json; charset=utf-8", "{\"status\":\"ok\",\"engine\":\"C++17\"}"};
    if (path == "/api/reset") {
        try {
            paging::SwapManager swap(1, std::filesystem::path("swap") / "swap.txt");
            swap.clear();
            return {200, "application/json; charset=utf-8", "{\"status\":\"reset\"}"};
        } catch (const std::exception& error) {
            return {500, "application/json; charset=utf-8",
                    "{\"error\":\"" + jsonEscape(error.what()) + "\"}"};
        }
    }
    if (path != "/api/simulate") return {404, "application/json; charset=utf-8", "{\"error\":\"Route not found.\"}"};

    try {
        const auto query = parseQuery(question == std::string::npos ? "" : target.substr(question + 1));
        const std::size_t frames = parseSize(query, "frames", 1, 16);
        const std::size_t swapSlots = parseSize(query, "swap", 1, 32);
        const auto algorithmValue = query.find("algorithm");
        if (algorithmValue == query.end()) throw std::invalid_argument("Missing required query parameter: algorithm");
        const auto referencesValue = query.find("references");
        if (referencesValue == query.end()) throw std::invalid_argument("Missing required query parameter: references");

        bool persistSwap = true;
        const auto persistValue = query.find("persist");
        if (persistValue != query.end()) {
            if (persistValue->second == "0" || persistValue->second == "false") persistSwap = false;
            else if (persistValue->second != "1" && persistValue->second != "true") {
                throw std::invalid_argument("Parameter 'persist' must be 0 or 1.");
            }
        }
        const auto algorithm = paging::parseAlgorithm(algorithmValue->second);
        const auto references = parseReferences(referencesValue->second);
        const auto swapFile = persistSwap ? std::filesystem::path("swap") / "swap.txt"
                                          : std::filesystem::path{};
        paging::MemoryManager manager(frames, swapSlots, swapFile);
        return {200, "application/json; charset=utf-8", serializeResult(manager.run(references, algorithm))};
    } catch (const std::invalid_argument& error) {
        return {400, "application/json; charset=utf-8",
                "{\"error\":\"" + jsonEscape(error.what()) + "\"}"};
    } catch (const std::exception& error) {
        return {500, "application/json; charset=utf-8",
                "{\"error\":\"" + jsonEscape(error.what()) + "\"}"};
    }
}

void closeSocket(SocketHandle socket) {
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

bool sendAll(SocketHandle socket, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
#ifdef _WIN32
        const int count = send(socket, data.data() + sent,
                               static_cast<int>(std::min<std::size_t>(data.size() - sent, 1U << 20)), 0);
#else
        const auto count = send(socket, data.data() + sent, data.size() - sent, 0);
#endif
        if (count <= 0) return false;
        sent += static_cast<std::size_t>(count);
    }
    return true;
}

void serveClient(SocketHandle client) {
    std::string request;
    char buffer[2048];
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < 16384) {
#ifdef _WIN32
        const int count = recv(client, buffer, sizeof(buffer), 0);
#else
        const auto count = recv(client, buffer, sizeof(buffer), 0);
#endif
        if (count <= 0) return;
        request.append(buffer, static_cast<std::size_t>(count));
    }

    std::istringstream requestStream(request);
    std::string method;
    std::string target;
    std::string version;
    requestStream >> method >> target >> version;
    HttpResponse response;
    if (method.empty() || target.empty() || version.rfind("HTTP/", 0) != 0) {
        response = {400, "application/json; charset=utf-8", "{\"error\":\"Malformed HTTP request.\"}"};
    } else {
        response = handleRequest(method, target);
    }

    std::ostringstream header;
    header << "HTTP/1.1 " << response.status << ' ' << statusText(response.status) << "\r\n"
           << "Content-Type: " << response.contentType << "\r\n"
           << "Content-Length: " << response.body.size() << "\r\n"
           << "Access-Control-Allow-Origin: *\r\n"
           << "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
           << "Access-Control-Allow-Headers: Content-Type\r\n"
           << "X-Content-Type-Options: nosniff\r\n"
           << "Cache-Control: no-store\r\n"
           << "Connection: close\r\n\r\n";
    sendAll(client, header.str() + response.body);
}

} // namespace

int main(int argc, char* argv[]) {
    int port = 8080;
    if (argc == 3 && std::string(argv[1]) == "--port") {
        try {
            std::size_t consumed = 0;
            port = std::stoi(argv[2], &consumed);
            if (consumed != std::string(argv[2]).size() || port < 1024 || port > 65535) {
                throw std::invalid_argument("invalid port");
            }
        } catch (const std::exception&) {
            std::cerr << "Usage: DemandPagingSimulator [--port 1024-65535]\n";
            return 2;
        }
    } else if (argc != 1) {
        std::cerr << "Usage: DemandPagingSimulator [--port 1024-65535]\n";
        return 2;
    }

#ifdef _WIN32
    WSADATA winsockData;
    if (WSAStartup(MAKEWORD(2, 2), &winsockData) != 0) {
        std::cerr << "Could not initialize Winsock.\n";
        return 1;
    }
#endif

    SocketHandle server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == kInvalidSocket) {
        std::cerr << "Could not create the local HTTP server socket.\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    int reuseAddress = 1;
#ifdef _WIN32
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuseAddress), sizeof(reuseAddress));
#else
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuseAddress, sizeof(reuseAddress));
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(port));
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(server, 8) != 0) {
        std::cerr << "Could not listen on http://127.0.0.1:" << port
                  << ". The port may already be in use.\n";
        closeSocket(server);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    std::cout << "Demand Paging Simulator is running at http://127.0.0.1:" << port
              << "/  (press Ctrl+C to stop)\n";
    while (true) {
        SocketHandle client = accept(server, nullptr, nullptr);
        if (client == kInvalidSocket) continue;
        serveClient(client);
        closeSocket(client);
    }
}
