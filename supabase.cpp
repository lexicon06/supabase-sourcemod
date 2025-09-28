#include "supabase.h"
#include <fstream>
#include <sstream>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#include <curl/curl.h>
#endif

SupabaseExtension g_SupabaseExtension;
SMEXT_LINK(&g_SupabaseExtension);

// Native function table
const sp_nativeinfo_t supabase_natives[] = 
{
    {"Supabase_Configure",     Native_Supabase_Configure},
    {"Supabase_IsConfigured",  Native_Supabase_IsConfigured},
    {"Supabase_Insert",        Native_Supabase_Insert},
    {"Supabase_Select",        Native_Supabase_Select},
    {"Supabase_Update",        Native_Supabase_Update},
    {"Supabase_Delete",        Native_Supabase_Delete},
    {"Supabase_ExecuteQuery",  Native_Supabase_ExecuteQuery},
    {NULL, NULL}
};

bool SupabaseExtension::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
    configured = false;
    nextQueryId = 1;
    
    // Initialize HTTP client
    try {
        httpClient = std::make_unique<HttpClient>();
    } catch (const std::exception& e) {
        snprintf(error, maxlength, "Failed to initialize HTTP client: %s", e.what());
        return false;
    }
    
    // Load configuration if exists
    if (!LoadConfig()) {
        g_pSM->LogMessage(myself, "No configuration found. Use Supabase_Configure to set up.");
    }
    
    // Register natives
    sharesys->AddNatives(myself, supabase_natives);
    
    g_pSM->LogMessage(myself, "Supabase extension loaded successfully");
    return true;
}

void SupabaseExtension::SDK_OnUnload()
{
    activeQueries.clear();
    httpClient.reset();
    g_pSM->LogMessage(myself, "Supabase extension unloaded");
}

bool SupabaseExtension::SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
    return true;
}

void SupabaseExtension::SDK_OnAllLoaded()
{
    g_pSM->LogMessage(myself, "Supabase extension fully loaded");
}

bool SupabaseExtension::Configure(const char* url, const char* apiKey)
{
    if (!url || !apiKey || strlen(url) == 0 || strlen(apiKey) == 0) {
        return false;
    }
    
    supabaseUrl = std::string(url);
    this->apiKey = std::string(apiKey);
    
    // Remove trailing slash from URL
    if (supabaseUrl.back() == '/') {
        supabaseUrl.pop_back();
    }
    
    configured = true;
    SaveConfig();
    
    g_pSM->LogMessage(myself, "Supabase configured with URL: %s", supabaseUrl.c_str());
    return true;
}

int SupabaseExtension::Insert(const char* table, const char* jsonData, 
                             SupabaseCallback callback, void* userData)
{
    return ExecuteQuery(table, "INSERT", jsonData, "", callback, userData);
}

int SupabaseExtension::Select(const char* table, const char* filters, 
                             SupabaseCallback callback, void* userData)
{
    return ExecuteQuery(table, "SELECT", "", filters, callback, userData);
}

int SupabaseExtension::Update(const char* table, const char* jsonData, const char* filters,
                             SupabaseCallback callback, void* userData)
{
    return ExecuteQuery(table, "UPDATE", jsonData, filters, callback, userData);
}

int SupabaseExtension::Delete(const char* table, const char* filters,
                             SupabaseCallback callback, void* userData)
{
    return ExecuteQuery(table, "DELETE", "", filters, callback, userData);
}

int SupabaseExtension::ExecuteQuery(const char* table, const char* operation, 
                                   const char* data, const char* filters,
                                   SupabaseCallback callback, void* userData)
{
    if (!configured) {
        if (callback) callback(false, "Supabase not configured", userData);
        return -1;
    }
    
    if (!table || !operation) {
        if (callback) callback(false, "Invalid parameters", userData);
        return -1;
    }
    
    SupabaseQuery query;
    query.table = std::string(table);
    query.operation = std::string(operation);
    query.data = data ? std::string(data) : "";
    query.filters = filters ? std::string(filters) : "";
    query.callback = callback;
    query.userData = userData;
    query.queryId = nextQueryId++;
    
    // Validate JSON data if provided
    if (!query.data.empty() && !ValidateJsonData(query.data.c_str())) {
        if (callback) callback(false, "Invalid JSON data", userData);
        return -1;
    }
    
    // Store query for processing
    activeQueries[query.queryId] = query;
    
    // Process immediately (in real implementation, you might queue these)
    ProcessQueries();
    
    return query.queryId;
}

bool SupabaseExtension::ExecuteQuerySync(const char* table, const char* operation, 
                                        const char* data, const char* filters, std::string& response)
{
    if (!configured) {
        response = "Supabase not configured";
        return false;
    }
    
    std::string url = BuildUrl(table, operation, filters);
    std::map<std::string, std::string> headers;
    
    // Build headers
    headers["apikey"] = apiKey;
    headers["Authorization"] = "Bearer " + apiKey;
    headers["Content-Type"] = "application/json";
    headers["Prefer"] = "return=representation";
    
    HttpClient::Response httpResponse;
    
    if (strcmp(operation, "SELECT") == 0) {
        httpResponse = httpClient->Get(url, headers);
    }
    else if (strcmp(operation, "INSERT") == 0) {
        httpResponse = httpClient->Post(url, data ? std::string(data) : "", headers);
    }
    else if (strcmp(operation, "UPDATE") == 0) {
        httpResponse = httpClient->Put(url, data ? std::string(data) : "", headers);
    }
    else if (strcmp(operation, "DELETE") == 0) {
        httpResponse = httpClient->Delete(url, headers);
    }
    else {
        response = "Invalid operation";
        return false;
    }
    
    response = httpResponse.body;
    return httpResponse.success && httpResponse.statusCode >= 200 && httpResponse.statusCode < 300;
}

void SupabaseExtension::ProcessQueries()
{
    // Process all active queries
    for (auto it = activeQueries.begin(); it != activeQueries.end();) {
        SupabaseQuery& query = it->second;
        
        std::string response;
        bool success = ExecuteQuerySync(query.table.c_str(), query.operation.c_str(),
                                       query.data.c_str(), query.filters.c_str(), response);
        
        // Execute callback
        if (query.callback) {
            query.callback(success, response.c_str(), query.userData);
        }
        
        // Remove processed query
        it = activeQueries.erase(it);
    }
}

std::string SupabaseExtension::BuildUrl(const char* table, const char* operation, const char* filters)
{
    std::string url = supabaseUrl + "/rest/v1/" + std::string(table);
    
    if (filters && strlen(filters) > 0) {
        url += "?" + std::string(filters);
    }
    
    return url;
}

bool SupabaseExtension::ValidateJsonData(const char* data)
{
    if (!data || strlen(data) == 0) return true;
    
    // Simple JSON validation - just check for balanced braces
    int braceCount = 0;
    bool inString = false;
    bool escaped = false;
    
    for (size_t i = 0; i < strlen(data); i++) {
        char c = data[i];
        
        if (escaped) {
            escaped = false;
            continue;
        }
        
        if (c == '\\') {
            escaped = true;
            continue;
        }
        
        if (c == '"') {
            inString = !inString;
            continue;
        }
        
        if (!inString) {
            if (c == '{') braceCount++;
            else if (c == '}') braceCount--;
        }
    }
    
    return braceCount == 0 && !inString;
}

bool SupabaseExtension::LoadConfig()
{
    std::ifstream configFile("supabase.cfg");
    if (!configFile.is_open()) {
        return false;
    }
    
    std::string url, key;
    if (std::getline(configFile, url) && std::getline(configFile, key)) {
        return Configure(url.c_str(), key.c_str());
    }
    
    return false;
}

void SupabaseExtension::SaveConfig()
{
    std::ofstream configFile("supabase.cfg");
    if (configFile.is_open()) {
        configFile << supabaseUrl << std::endl;
        configFile << apiKey << std::endl;
        configFile.close();
    }
}

// ========== HttpClient Implementation ==========

#ifndef _WIN32
struct CurlResponseData {
    std::string data;
};

size_t HttpClient::WriteCallback(void* contents, size_t size, size_t nmemb, std::string* data)
{
    size_t totalSize = size * nmemb;
    data->append((char*)contents, totalSize);
    return totalSize;
}
#endif

HttpClient::HttpClient() : initialized(false), curlHandle(nullptr)
{
#ifdef _WIN32
    initialized = true;
#else
    if (curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK) {
        curlHandle = curl_easy_init();
        initialized = (curlHandle != nullptr);
    }
#endif
}

HttpClient::~HttpClient()
{
#ifndef _WIN32
    if (curlHandle) {
        curl_easy_cleanup((CURL*)curlHandle);
    }
    curl_global_cleanup();
#endif
}

HttpClient::Response HttpClient::Get(const std::string& url, const std::map<std::string, std::string>& headers)
{
    return MakeRequest("GET", url, "", headers);
}

HttpClient::Response HttpClient::Post(const std::string& url, const std::string& data, 
                                     const std::map<std::string, std::string>& headers)
{
    return MakeRequest("POST", url, data, headers);
}

HttpClient::Response HttpClient::Put(const std::string& url, const std::string& data,
                                    const std::map<std::string, std::string>& headers)
{
    return MakeRequest("PUT", url, data, headers);
}

HttpClient::Response HttpClient::Delete(const std::string& url, const std::map<std::string, std::string>& headers)
{
    return MakeRequest("DELETE", url, "", headers);
}

HttpClient::Response HttpClient::MakeRequest(const std::string& method, const std::string& url, 
                                           const std::string& data, const std::map<std::string, std::string>& headers)
{
    Response response;
    response.success = false;
    response.statusCode = 0;
    
    if (!initialized) {
        response.body = "HTTP client not initialized";
        return response;
    }
    
#ifdef _WIN32
    // Windows implementation would go here using WinHTTP
    response.body = "Windows HTTP not implemented yet";
    response.statusCode = 500;
#else
    // Linux implementation using CURL
    CURL* curl = (CURL*)curlHandle;
    if (!curl) {
        response.body = "CURL handle not available";
        return response;
    }
    
    std::string responseData;
    struct curl_slist* headerList = nullptr;
    
    // Set URL
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseData);
    
    // Set method
    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
    } else if (method == "PUT") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
    } else if (method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }
    
    // Set headers
    for (const auto& header : headers) {
        std::string headerStr = header.first + ": " + header.second;
        headerList = curl_slist_append(headerList, headerStr.c_str());
    }
    if (headerList) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    }
    
    // Perform request
    CURLcode res = curl_easy_perform(curl);
    
    if (res == CURLE_OK) {
        long responseCode;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
        response.statusCode = (int)responseCode;
        response.body = responseData;
        response.success = true;
    } else {
        response.body = curl_easy_strerror(res);
        response.statusCode = 0;
    }
    
    // Cleanup
    if (headerList) {
        curl_slist_free_all(headerList);
    }
    
    // Reset curl for next use
    curl_easy_reset(curl);
#endif
    
    return response;
}

// ========== Native Functions ==========

cell_t Native_Supabase_Configure(IPluginContext *pContext, const cell_t *params)
{
    char *url, *apiKey;
    pContext->LocalToString(params[1], &url);
    pContext->LocalToString(params[2], &apiKey);
    
    return g_SupabaseExtension.Configure(url, apiKey) ? 1 : 0;
}

cell_t Native_Supabase_IsConfigured(IPluginContext *pContext, const cell_t *params)
{
    return g_SupabaseExtension.IsConfigured() ? 1 : 0;
}

cell_t Native_Supabase_Insert(IPluginContext *pContext, const cell_t *params)
{
    char *table, *data;
    pContext->LocalToString(params[1], &table);
    pContext->LocalToString(params[2], &data);
    
    // For now, execute synchronously and return success/failure
    std::string response;
    bool success = g_SupabaseExtension.ExecuteQuerySync(table, "INSERT", data, "", response);
    
    return success ? 1 : 0;
}

cell_t Native_Supabase_Select(IPluginContext *pContext, const cell_t *params)
{
    char *table, *filters;
    pContext->LocalToString(params[1], &table);
    pContext->LocalToString(params[2], &filters);
    
    std::string response;
    bool success = g_SupabaseExtension.ExecuteQuerySync(table, "SELECT", "", filters, response);
    
    // Store response in plugin's buffer
    pContext->StringToLocal(params[3], params[4], response.c_str());
    
    return success ? 1 : 0;
}

cell_t Native_Supabase_Update(IPluginContext *pContext, const cell_t *params)
{
    char *table, *data, *filters;
    pContext->LocalToString(params[1], &table);
    pContext->LocalToString(params[2], &data);
    pContext->LocalToString(params[3], &filters);
    
    std::string response;
    bool success = g_SupabaseExtension.ExecuteQuerySync(table, "UPDATE", data, filters, response);
    
    return success ? 1 : 0;
}

cell_t Native_Supabase_Delete(IPluginContext *pContext, const cell_t *params)
{
    char *table, *filters;
    pContext->LocalToString(params[1], &table);
    pContext->LocalToString(params[2], &filters);
    
    std::string response;
    bool success = g_SupabaseExtension.ExecuteQuerySync(table, "DELETE", "", filters, response);
    
    return success ? 1 : 0;
}

cell_t Native_Supabase_ExecuteQuery(IPluginContext *pContext, const cell_t *params)
{
    char *table, *operation, *data, *filters;
    pContext->LocalToString(params[1], &table);
    pContext->LocalToString(params[2], &operation);
    pContext->LocalToString(params[3], &data);
    pContext->LocalToString(params[4], &filters);
    
    std::string response;
    bool success = g_SupabaseExtension.ExecuteQuerySync(table, operation, data, filters, response);
    
    // Store response in plugin's buffer
    pContext->StringToLocal(params[5], params[6], response.c_str());
    
    return success ? 1 : 0;
}
