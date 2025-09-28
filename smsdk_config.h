#ifndef _INCLUDE_SOURCEMOD_EXTENSION_CONFIG_H_  
#define _INCLUDE_SOURCEMOD_EXTENSION_CONFIG_H_  
  
#define SMEXT_CONF_NAME         "Supabase"  
#define SMEXT_CONF_DESCRIPTION  "Supabase PostgreSQL database driver"  
#define SMEXT_CONF_VERSION      "1.0.0"  
#define SMEXT_CONF_AUTHOR       "SourceMod Team"  
#define SMEXT_CONF_URL          "http://www.sourcemod.net/"  
#define SMEXT_CONF_LOGTAG       "SUPABASE"  
#define SMEXT_CONF_LICENSE      "GPL"  
#define SMEXT_CONF_DATESTRING   __DATE__  
  
#define SMEXT_LINK(name) SDKExtension *g_pExtensionIface = name;  
#define SMEXT_CONF_METAMOD  
#define SMEXT_ENABLE_DBMANAGER  
  
#endif
