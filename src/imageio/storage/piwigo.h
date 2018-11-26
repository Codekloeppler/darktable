#define DT_PIWIGO_VERSION 0                 // module version
#define DT_PIWIGO_NAME    "Piwigo"          // module name
#define DT_PIWIGO_FAILED  FALSE
#define DT_PIWIGO_SUCCESS TRUE
#define DT_PIWIGO_NO      DT_PIWIGO_FAILED
#define DT_PIWIGO_YES     DT_PIWIGO_SUCCESS
#define DT_PIWIGO_FINISHED 0

#define DT_PWG_POSTDATA_MAXLEN 8000         // maximum length of POST data string length

// No maximum dimension
#define DT_PIWIGO_DIMENSION_MAX   0         
// No recommended dimension
#define DT_PIWIGO_DIMENSION_BEST  0         
#define DT_PIWIGO_API     "/ws.php?format=json&method="
#define DT_PIWIGO_SITE_MAXLEN ( 1024*sizeof(char) )
#define DT_PIWIGO_METHOD_MAXLEN ( 8192*sizeof(char) ) 
#define DT_PIWIGO_URL_MAXLEN ( DT_PIWIGO_SITE_MAXLEN + DT_PIWIGO_METHOD_MAXLEN )

// Supported Piwigo API method sections
#define DT_PIWIGO_PREFIX "pwg"
#define DT_PIWIGO_SESSION DT_PIWIGO_PREFIX ".session"
#define DT_PIWIGO_CATEGORIES DT_PIWIGO_PREFIX ".categories"
#define DT_PIWIGO_IMAGES DT_PIWIGO_PREFIX ".images"

#define DT_PIWIGO_API_SESSION_LOGIN DT_PIWIGO_API DT_PIWIGO_SESSION ".login"
#define DT_PIWIGO_API_SESSION_LOGOUT DT_PIWIGO_API DT_PIWIGO_SESSION ".logout"
#define DT_PIWIGO_API_SESSION_GET_STATUS DT_PIWIGO_API DT_PIWIGO_SESSION ".getStatus"
#define DT_PIWIGO_API_CATEGORY_GETLIST DT_PIWIGO_API DT_PIWIGO_CATEGORIES ".getList"

#define DT_PIWIGO_API_CATEGORY_GET_ADMINLIST DT_PIWIGO_API DT_PIWIGO_CATEGORIES ".getAdminList"

#define DT_PIWIGO_API_CATEGORY_ADD DT_PIWIGO_API DT_PIWIGO_CATEGORIES ".Add"

// PIWIGO API METHOD OPTIONS
#define PWG_GETLIST_RECURSIVE "&recursive=true"
#define PWG_GETLIST_TREE "&tree_output=true"

typedef enum piwigo_methods {
  none = 0,
  getStatus,
  login,
  logout,
  addImages,
  getCategories
} piwigo_methods_t;

typedef struct dt_module_imageio_storage_piwigo_auth
{
  gchar *username;
  gboolean registered;
  gchar *token;
  gboolean sslPeer;
} dt_module_imageio_storage_piwigo_auth_t;


typedef struct dt_module_imageio_storage_piwigo_context
{
  dt_module_imageio_storage_piwigo_auth_t *auth;
  CURL *curl;
  char *site;
  char *path;
  dt_variables_params_t *params;
  JsonParser *parser;
  gchar *supported_file_types;
} dt_module_imageio_storage_piwigo_context_t;


typedef struct dt_module_imageio_storage_piwigo_ui
{
  GtkWidget *site;
  GtkWidget *site_label;
  GtkWidget *album;
  GtkWidget *album_label;
  GtkWidget *username;
  GtkWidget *username_label;
  GtkWidget *password;
  GtkWidget *password_label;
  GtkWidget *login_button;
  dt_module_imageio_storage_piwigo_context_t *context;
} dt_module_imageio_storage_piwigo_ui_t;

// STATIC FUNCTION PROTOTYPES
static void 
pwg_init(dt_module_imageio_storage_piwigo_ui_t **ui);

static void 
pwg_login_button_clicked(GtkWidget *widget, dt_imageio_module_storage_t *self);

static void
pwg_password_entry_activated(GtkWidget *widget, dt_imageio_module_storage_t *self);

static void 
config_changed_callback(GtkEntry *entry, gpointer user_data);

static size_t 
curl_write_data_callback(void *ptr, size_t size, size_t nmemb, void *data);

static JsonObject *
parse_json_response(dt_module_imageio_storage_piwigo_context_t *context, GString *response);

static gboolean 
pwg_login(dt_module_imageio_storage_piwigo_ui_t *ui);

static gboolean 
pwg_logout(dt_module_imageio_storage_piwigo_ui_t *ui);

static gboolean 
pwg_call(dt_module_imageio_storage_piwigo_ui_t *ui, GString *response, const char* postdata, const char *method);

//static void
//pwg_debug(const char *format, const char *file, size_t line, ...);

static int
pwg_finalize_store(gpointer user_data);

//static int pwg_createCategoryPath(dt_module_imageio_storage_piwigo_ui_t *ui, const char * path);
//static void ui_refresh_albums_fill(PicasaAlbum *album, GtkListStore *list_store);
