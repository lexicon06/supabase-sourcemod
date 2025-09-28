#ifndef _INCLUDE_SUPABASE_EXTENSION_H_  
#define _INCLUDE_SUPABASE_EXTENSION_H_  
  
#include "smsdk_ext.h"  
#include <string>  
#include <map>  
#include <vector>  
#include <memory>  
  
// Forward declarations  
struct SupabaseQuery;  
class HttpClient;  
  
// Callback types  
typedef void (*SupabaseCallback)(bool success, const char* response, void* data);  
  
struct SupabaseQuery {  
    std::string table;  
    std::string operation; // SELECT, INSERT, UPDATE, DELETE  
    std::string data;  
    std::string filters;  
    SupabaseCallback callback;  
    void* userData;  
    int queryId;  
};  
  
class SupabaseExtension : public SDKExtension  
{  
public:  
    virtual bool SDK_OnLoad(char *error, size_t maxlength, bool late);  
    virtual void SDK_OnUnload();  
    virtual void SDK_OnAllLoaded();  
      
#if defined SMEXT_CONF_METAMOD  
    virtual bool SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late);  
#endif  
  
public:  
    // Configuration  
    bool Configure(const char* url, const char* apiKey);  
    bool IsConfigured() const { return configured; }  
      
    // Database operations  
    int ExecuteQuery(const char* table, const char* operation, const char* data,   
                    const char* filters, SupabaseCallback callback, void* userData);  
    bool ExecuteQuerySync(const char* table, const char* operation, const char* data,   
                         const char* filters, std::string& response);  
      
    // Specific operations  
    int Insert(const char* table, const char* jsonData, SupabaseCallback callback, void* userData);  
    int Select(const char* table, const char* filters, SupabaseCallback callback, void* userData);  
    int Update(const char* table, const char* jsonData, const char* filters,   
               SupabaseCallback callback, void* userData);  
    int Delete(const char* table, const char* filters, SupabaseCallback callback, void* userData);  
      
    // Auth operations  
    bool AuthenticateUser(const char* email, const char* password, std::string& token);  
    bool ValidateToken(const char* token);  
      
private:  
    bool configured = false;  
    std::string supabaseUrl;  
    std::string apiKey;  
    std::unique_ptr<HttpClient> httpClient;  
    std::map<int, SupabaseQuery> activeQueries;  
    int nextQueryId = 1;  
      
    void ProcessQueries();  
    std::string BuildUrl(const char* table, const char* operation, const char* filters);  
    std::string BuildHeaders();  
    bool ValidateJsonData(const char* data);  
      
    // Config file management  
    bool LoadConfig();  
    void SaveConfig();  
};  
  
// HTTP Client for REST API calls  
class HttpClient   
{  
public:  
    HttpClient();  
    ~HttpClient();  
      
    struct Response {  
        int statusCode = 0;  
        std::string body;  
        std::map<std::string, std::string> headers;  
        bool success = false;  
    };  
      
    Response Get(const std::string& url, const std::map<std::string, std::string>& headers = {});  
    Response Post(const std::string& url, const std::string& data,   
                 const std::map<std::string, std::string>& headers = {});  
    Response Put(const std::string& url, const std::string& data,  
                const std::map<std::string, std::string>& headers = {});  
    Response Delete(const std::string& url, const std::map<std::string, std::string>& headers = {});  
      
private:  
    bool initialized = false;  
    void* curlHandle = nullptr; // CURL* but avoiding include  
      
    Response MakeRequest(const std::string& method, const std::string& url,   
                        const std::string& data, const std::map<std::string, std::string>& headers);  
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* data);  
};  
  
// Global instance  
extern SupabaseExtension g_SupabaseExtension;  
  
// Native functions for SourcePawn  
cell_t Native_Supabase_Configure(IPluginContext *pContext, const cell_t *params);  
cell_t Native_Supabase_IsConfigured(IPluginContext *pContext, const cell_t *params);  
cell_t Native_Supabase_Insert(IPluginContext *pContext, const cell_t *params);  
cell_t Native_Supabase_Select(IPluginContext *pContext, const cell_t *params);  
cell_t Native_Supabase_Update(IPluginContext *pContext, const cell_t *params);  
cell_t Native_Supabase_Delete(IPluginContext *pContext, const cell_t *params);  
cell_t Native_Supabase_ExecuteQuery(IPluginContext *pContext, const cell_t *params);  
  
extern const sp_nativeinfo_t supabase_natives[];  
  
#endif // _INCLUDE_SUPABASE_EXTENSION_H_
