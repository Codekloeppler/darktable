/*
  This file is part of darktable,
  copyright (c) 2010-2011 Jose Carlos Garcia Sogo

  darktable is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  darktable is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

/* Ablauf beim Darktable Start:

* Laden der/des Plugins -> init()
* Anzeige des Plugins   -> gui_init()
                          => Lesen der Konfigurationsdaten 

* Beenden von Dartable  -> gui_cleanup()

*/

#include "bauhaus/bauhaus.h"
#include "common/darktable.h"
#include "common/image.h"
#include "common/image_cache.h"
#include "common/imageio.h"
#include "common/imageio_module.h"
#include "common/metadata.h"
#include "common/pwstorage/pwstorage.h"
#include "common/tags.h"
#include "control/conf.h"
#include "dtgtk/button.h"
#include "dtgtk/paint.h"
#include "gui/gtk.h"
#include "gui/gtkentry.h"
#include "imageio/storage/imageio_storage_api.h"
#include <curl/curl.h>
#include <json-glib/json-glib.h>
#include <stdlib.h>

#define DT_PIWIGO_DEBUG 1

#define DT_PIWIGO_VERSION 0                 // module version
#define DT_PIWIGO_NAME    "Piwigo"          // module name
#define DT_PIWIGO_FAILED  0
#define DT_PIWIGO_SUCCESS 1
#define DT_PIWIGO_NO      DT_PIWIGO_FAILED
#define DT_PIWIGO_YES     DT_PIWIGO_SUCCESS
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

// module supports darktable release 2.x
DT_MODULE(2)

typedef enum piwigo_methods {
  none = 0,
  getStatus,
  login,
  logout,
  addImages,
  getCategories
} piwigo_methods_t;


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
} dt_module_imageio_storage_piwigo_ui_t;

typedef struct dt_module_imageio_storage_piwigo_auth
{
  gchar *username;
  gboolean registered;
  gchar *token;
  gboolean sslPeer;
} dt_module_imageio_storage_piwigo_auth_t;

typedef struct dt_module_imageio_storage_piwigo_context
{
  dt_module_imageio_storage_piwigo_ui_t *ui;
  dt_module_imageio_storage_piwigo_auth_t *auth;
  char *site;
  char *path;
  dt_variables_params_t *params;
  JsonParser *parser;
  gchar *supported_file_types;
} dt_module_imageio_storage_piwigo_context_t;


// STATIC FUNCTION PROTOTYPES
static void init_context(dt_module_imageio_storage_piwigo_context_t **context);
static void clicked_login_button(GtkWidget *widget, dt_imageio_module_storage_t *self);
static void config_changed_callback(GtkEntry *entry, gpointer user_data);
static size_t curl_write_data_callback(void *ptr, size_t size, size_t nmemb, void *data);
static JsonObject *parse_json_response(dt_module_imageio_storage_piwigo_context_t *context, GString *response);
static gboolean pwg_login(dt_module_imageio_storage_piwigo_context_t *context);
static gboolean pwg_logout(dt_module_imageio_storage_piwigo_context_t *context);
//static gboolean callREST(GString *response, dt_module_imageio_storage_piwigo_context_t *context, GString *method);
static gboolean callPWG(dt_module_imageio_storage_piwigo_context_t *context, GString *response, const char* postdata, const char *method);
static int pwg_createCategoryPath(dt_module_imageio_storage_piwigo_context_t *context, const char * path);
static void pwg_debug(const char *format, const char *file, size_t line, ...);

/* get module version */
int version()
{
  return DT_PIWIGO_VERSION;
}

/* get translated module name */
const char *name(const struct dt_imageio_module_storage_t *self)
{
  return _(DT_PIWIGO_NAME);
}

void init(dt_imageio_module_storage_t *self)
{
  dt_module_imageio_storage_piwigo_context_t * context = (dt_module_imageio_storage_piwigo_context_t *) self->gui_data;
  init_context(&context);
}

/* allow the module to initialize itself */
void  init_context(dt_module_imageio_storage_piwigo_context_t **context)
{
  if ( !*context )
  {
    *context = (dt_module_imageio_storage_piwigo_context_t *)malloc(sizeof(dt_module_imageio_storage_piwigo_context_t));
    (*context)->ui = (dt_module_imageio_storage_piwigo_ui_t *) malloc(sizeof(dt_module_imageio_storage_piwigo_ui_t));
    (*context)->auth = (dt_module_imageio_storage_piwigo_auth_t *) malloc(sizeof(dt_module_imageio_storage_piwigo_auth_t));
  }
      
  (*context)->auth->sslPeer = TRUE;
  (*context)->auth->registered = FALSE;
  (*context)->auth->username = NULL;
  (*context)->auth->token = NULL;
  (*context)->site = NULL;
  (*context)->path = NULL;
  (*context)->params = NULL;
  (*context)->parser = json_parser_new ();
  (*context)->supported_file_types = NULL;  
  
  return;
  
}

/* construct module widget */
void gui_init(struct dt_imageio_module_storage_t *self)
{
  /*
   * Layout desired:
   *
   * v------------------------------------|
   * b--hboxUrl---------------------------|
   * o _labelUrl______   _entryUrl______  |
   * x------------------------------------|
   * |--hboxUser--------------------------|
   * | _labelUser_____   _entyUser______  |
   * |------------------------------------|
   * |--hboxPass--------------------------|
   * | _labelPass_____   _entryPass_____  |
   * |------------------------------------|
   * |         __buttonLogin__            |    <- Change button label to "Logout", if logged in - "Login" otherwise.
   * |                                    |
   * |--hboxPath--------------------------|
   * | _entryPath_______________________  |
   * |------------------------------------|
   */
  GtkWidget *vbox;
  GtkWidget *hboxUrl;
  GtkWidget *hboxUser;
  GtkWidget *hboxPass;
  GtkWidget *hboxLogin;
  GtkWidget *hboxAlbum;
  dt_module_imageio_storage_piwigo_context_t *context = NULL;

  init_context(&context);
  // Prevent double initialization (might be already done in init()!?)

  self->gui_data = (void *)context;
  
  //darktable module layout container
  self->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, DT_PIXEL_APPLY_DPI(5));

  // LAYOUT CONTAINERS
  // add module preferences layout container
  vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, DT_PIXEL_APPLY_DPI(8));
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(vbox), TRUE, FALSE, 0);

  // add url preference layout container
  hboxUrl = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, DT_PIXEL_APPLY_DPI(8));
  gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(hboxUrl), TRUE, FALSE, 0);

  // add username preference layout container
  hboxUser = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, DT_PIXEL_APPLY_DPI(8));
  gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(hboxUser), TRUE, FALSE, 0);

  // add password preference layout container
  hboxPass = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, DT_PIXEL_APPLY_DPI(8));
  gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(hboxPass), TRUE, FALSE, 0);

  // album path preference layout container
  hboxLogin = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, DT_PIXEL_APPLY_DPI(8));
  gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(hboxLogin), TRUE, TRUE, 0);
  
  // add album path preference layout container
  hboxAlbum = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, DT_PIXEL_APPLY_DPI(8));
  gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(hboxAlbum), TRUE, FALSE, 0);  

  // add site label
  context->ui->site_label = gtk_label_new(_("Server"));
  gtk_box_pack_start(GTK_BOX(hboxUrl), GTK_WIDGET(context->ui->site_label), TRUE, FALSE, 0);
  
  // add site input with tooltip
  context->ui->site = gtk_entry_new();
  gtk_box_pack_start(GTK_BOX(hboxUrl), GTK_WIDGET(context->ui->site), TRUE, TRUE, 0);
  gtk_widget_set_tooltip_text(context->ui->site, _("piwigo server site"));
  gtk_entry_set_width_chars(GTK_ENTRY(context->ui->site), 0);
  
  // fill site input with config  
  gchar *url = dt_conf_get_string("plugins/imageio/storage/piwigo/site");
  if(url)
  {
    gtk_entry_set_text(GTK_ENTRY(context->ui->site), url);
    g_free(url);
  }  
  // add callback to write changed config
  g_signal_connect(context->ui->site, "changed", G_CALLBACK(config_changed_callback), self);
  
  // disable darktable hotkeys, when site field has focus
  dt_gui_key_accel_block_on_focus_connect(GTK_WIDGET(context->ui->site));
  
  // add username label
  context->ui->username_label = gtk_label_new(_("Username"));
  gtk_box_pack_start(GTK_BOX(hboxUser), GTK_WIDGET(context->ui->username_label), TRUE, FALSE, 0);
  
  // add username input with tooltip
  context->ui->username = gtk_entry_new();
  gtk_box_pack_start(GTK_BOX(hboxUser), GTK_WIDGET(context->ui->username), TRUE, TRUE, 0);
  gtk_widget_set_tooltip_text(context->ui->username, _("piwigo server account"));
  gtk_entry_set_width_chars(GTK_ENTRY(context->ui->username), 0);
  
  // fill username input with config  
  gchar *user = dt_conf_get_string("plugins/imageio/storage/piwigo/username");
  if(user)
  {
    gtk_entry_set_text(GTK_ENTRY(context->ui->username), user);
    g_free(user);
  }
    // add callback to write changed config
  g_signal_connect(context->ui->username, "changed", G_CALLBACK(config_changed_callback), self);
  
  // disable darktable hotkeys, when username field has focus
  dt_gui_key_accel_block_on_focus_connect(GTK_WIDGET(context->ui->username));

    // add password label
  context->ui->password_label = gtk_label_new(_("Password"));
  gtk_box_pack_start(GTK_BOX(hboxPass), GTK_WIDGET(context->ui->password_label), TRUE, FALSE, 0);
  
  // add password input with tooltip
  context->ui->password = gtk_entry_new();
  gtk_box_pack_start(GTK_BOX(hboxPass), GTK_WIDGET(context->ui->password), TRUE, TRUE, 0);
  gtk_entry_set_visibility(GTK_ENTRY(context->ui->password), FALSE);
  gtk_entry_set_input_purpose(GTK_ENTRY(context->ui->password), GTK_INPUT_PURPOSE_PASSWORD);
  gtk_widget_set_tooltip_text(context->ui->password, _("piwigo server password"));
  gtk_entry_set_width_chars(GTK_ENTRY(context->ui->password), 0);
  
  // disable darktable hotkeys, when password field has focus
  dt_gui_key_accel_block_on_focus_connect(GTK_WIDGET(context->ui->password));
  
  // add login/logout button
  context->ui->login_button = gtk_button_new_with_label("Login");
  gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(context->ui->login_button), TRUE, TRUE, 0);
  g_signal_connect(G_OBJECT(context->ui->login_button), "clicked", G_CALLBACK(clicked_login_button), self);

  // add album label
  context->ui->album_label = gtk_label_new(_("Album"));
  gtk_box_pack_start(GTK_BOX(hboxAlbum), GTK_WIDGET(context->ui->album_label), TRUE, FALSE, 0);
  
  // add album input with extended tooltip
  context->ui->album = gtk_entry_new();
  gtk_box_pack_start(GTK_BOX(hboxAlbum), GTK_WIDGET(context->ui->album), TRUE, TRUE, 0);
  
  dt_gtkentry_setup_completion(GTK_ENTRY(context->ui->album), dt_gtkentry_get_default_path_compl_list());
  char *tooltip_text_path = dt_gtkentry_build_completion_tooltip_text(
      _("enter the path where to put exported images\n"
        "recognized variables:"),
      dt_gtkentry_get_default_path_compl_list());
  gtk_widget_set_tooltip_text(context->ui->album, tooltip_text_path);
  g_free(tooltip_text_path);
  gtk_entry_set_width_chars(GTK_ENTRY(context->ui->album), 0);
  
  // fill album input with config  
  gchar *album = dt_conf_get_string("plugins/imageio/storage/piwigo/album");
  if(album)
  {
    gtk_entry_set_text(GTK_ENTRY(context->ui->album), album);
    g_free(album);
  }
  // add callback to write changed config
  g_signal_connect(context->ui->album, "changed", G_CALLBACK(config_changed_callback), self);
  
  // disable darktable hotkeys, when album field has focus
  dt_gui_key_accel_block_on_focus_connect(GTK_WIDGET(context->ui->album));

  return;
}


/* destroy resources */
void gui_cleanup(struct dt_imageio_module_storage_t *self)
{
  dt_module_imageio_storage_piwigo_context_t * context = (dt_module_imageio_storage_piwigo_context_t*) self->gui_data;

  g_printf("%s:%d - %s() :: Context: %p\n",__FILE__,__LINE__,__func__,context);
  if ( context )
  {
    g_printf("%s:%d - %s() :: Context->ui: %p\n",__FILE__,__LINE__,__func__,context->ui);
    if ( context->ui ) free(context->ui);
    g_printf("%s:%d - %s() :: Context->auth: %p\n",__FILE__,__LINE__,__func__,context->auth);
    if ( context->auth)
    {
      g_printf("%s:%d - %s() :: Context->auth->username: %p\n",__FILE__,__LINE__,__func__,context->auth->username);
      if (context->auth->username) free(context->auth->username);
      g_printf("%s:%d - %s() :: Context->auth->token: %p\n",__FILE__,__LINE__,__func__,context->auth->token);
      if (context->auth->token) free(context->auth->token);
      free(context->auth);
    }
      g_printf("%s:%d - %s() :: Context->parser: %p\n",__FILE__,__LINE__,__func__,context->parser);
      if (context->parser) g_object_unref(context->parser);
      g_printf("%s:%d - %s() :: Context->supported_file_types: %p\n",__FILE__,__LINE__,__func__,context->supported_file_types);
    if (context->supported_file_types) { free(context->supported_file_types); }
    if (context->site) { free(context->site); }
    if (context->path) { free(context->path); }
    if (context->params) { free(context->params); }

    free(context);
  }

  return;
}
/* reset options to defaults */
void gui_reset(struct dt_imageio_module_storage_t *self)
{
  dt_module_imageio_storage_piwigo_context_t *context = self->gui_data;
  if ( !context || !context->auth )
  {
    gui_cleanup(self);
    init_context(&context);
  }
  else
  {
    context->auth->sslPeer = TRUE;
    context->auth->registered = FALSE;
    context->auth->username = NULL;
    context->auth->token = NULL;
    context->parser = json_parser_new ();
    context->supported_file_types = NULL;
  }
  // TODO - 20181102, codeklöppler: Reset gui elements content
  return;
}

/* try and see if this format is supported? */
int supported(struct dt_imageio_module_storage_t *self, struct dt_imageio_module_format_t *format)
{
  int result = DT_PIWIGO_NO;

  if ( ( strcmp(format->mime(NULL) ,"image/jpeg") == 0 )
       || ( strcmp(format->mime(NULL) ,"image/png") ==  0 ) 
       || ( strcmp(format->mime(NULL) ,"image/gif") ==  0 ) )
  {
    result = DT_PIWIGO_YES;
  }

  return result;
}

/* get storage max supported image dimension, return 0 if no dimension restrictions exists. */
int dimension(struct dt_imageio_module_storage_t *self, struct dt_imageio_module_data_t *data,
              uint32_t *width, uint32_t *height)
{
  return DT_PIWIGO_DIMENSION_MAX;
}
        
/* get storage recommended image dimension, return 0 if no recommendation exists. */
int recommended_dimension(struct dt_imageio_module_storage_t *self, struct dt_imageio_module_data_t *data,
                          uint32_t *width, uint32_t *height)
{
  return DT_PIWIGO_DIMENSION_BEST;
}

/* called once at the beginning (before exporting image), if implemented
   * can change the list of exported images (including a NULL list)
 */
int initialize_store(struct dt_imageio_module_storage_t *self, struct dt_imageio_module_data_t *data,
                     struct dt_imageio_module_format_t **format, struct dt_imageio_module_data_t **fdata,
                     GList **images, const gboolean high_quality, const gboolean upscale)
{
  // TODO 20181102, codeklöppler: Create album path, if it does'nt exist
  dt_module_imageio_storage_piwigo_context_t * context = (dt_module_imageio_storage_piwigo_context_t*) self->gui_data;
  //gchar *fullAlbumPath = NULL;
  //gchar *currentAlbum = NULL;
  //unsigned int parentCategory = 0;
  //const gchar* delimiter = "/";
  gboolean result = DT_PIWIGO_FAILED;
//  GString *methodCategoryGetList = g_string_new(DT_PIWIGO_API_CATEGORY_GETLIST "&recursive=true&tree_output=true");
  //GString *methodCategoryAdd = g_string_new(DT_PIWIGO_API_CATEGORY_ADD "&name=%s&parent=%u");
  //GString *response = g_string_new(NULL);
  int categoryId = -1;
  //dt_variables_params_t *d;;
//  d->vp->filename = input_dir;
//      d->vp->jobcode = "export";
//      d->vp->imgid = imgid;
//      d->vp->sequence = num;
//
  printf("UI Path: '%s'\n", gtk_entry_get_text(GTK_ENTRY(context->ui->album)));
  //pwg_debug("UI Path: '%s'\n", __FILE__,__LINE__,gtk_entry_get_text(GTK_ENTRY(context->ui->album)));
  //categoryId = pwg_createCategoryPath(context, dt_variables_expand( context->params, (char *) gtk_entry_get_text(GTK_ENTRY(context->ui->album)), TRUE));
  categoryId = pwg_createCategoryPath(context, "Exkurse/Hodenhagen");
  if ( categoryId >= 0 ) {
	  result = DT_PIWIGO_SUCCESS;
  }
  
  /*
  1. Feststellen, ob der Pfad zum Album exisitert
     a) Pfad existiert nicht -> Pfad anlegen.
     b) Pfad existiert -> Weiter mit 2.
  2. Für jedes zu exportierende Foto wiederholen
     a) Fotoname setzen
     b) author setzen
     c) Schlagworte/tags setzen
     d) Privacy-Level setzen
     e) Foto hochladen
     
  */
  
  
  return result;
  
}

/* this actually does the work */
int store(struct dt_imageio_module_storage_t *self, struct dt_imageio_module_data_t *self_data,
          const int imgid, struct dt_imageio_module_format_t *format, struct dt_imageio_module_data_t *fdata,
          const int num, const int total, const gboolean high_quality, const gboolean upscale,
          enum dt_colorspaces_color_profile_type_t icc_type, const gchar *icc_filename,
          enum dt_iop_color_intent_t icc_intent)
{
  dt_module_imageio_storage_piwigo_context_t *context = (dt_module_imageio_storage_piwigo_context_t *) self->gui_data;
  CURL *curl;
  char curl_errbuf[CURL_ERROR_SIZE];
  CURLcode res;
  GString *response = g_string_new("");
  int result = DT_PIWIGO_FAILED;

  if ( !context || !context->auth->registered )
  {
    return DT_PIWIGO_FAILED;
  }

  // Try to login
  curl = curl_easy_init();
  if (curl) {

    /* First set the URL that is about to receive our POST. This URL can
       just as well be a https:// URL if that is what should receive the
       data. */

    curl_easy_setopt(curl, CURLOPT_URL, "https://www.consensionis.de/Album" DT_PIWIGO_API "pwg.session.getStatus");
    
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_errbuf);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
#ifdef picasa_EXTRA_VERBOSE
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 2L);
#else
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
#endif

    if ( !context->auth->sslPeer )
    {
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, FALSE);
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_data_callback);
    /* Now specify the POST data */ 
    //curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "name=daniel&project=curl");
 
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
 
    /* Perform the request, res will get the return code */ 
    res = curl_easy_perform(curl);
    /* Check for errors */ 
    if(res == CURLE_OK)
    {
      result = DT_PIWIGO_SUCCESS;
      
      // parse the response
      JsonObject *respobj = parse_json_response(context, response);
      JsonObject *jsonResult = json_object_get_object_member(respobj,"result");
      context->auth->username = g_strdup(json_object_get_string_member(jsonResult, "username"));
      context->auth->token = g_strdup(json_object_get_string_member(jsonResult, "pwg_token"));
      context->supported_file_types = g_strdup(json_object_get_string_member(jsonResult, "upload_file_types"));
    }
    else
    {
      fprintf(stderr, "curl_easy_perform() failed: %s\n",
              curl_easy_strerror(res));
    }
    g_string_free(response, TRUE);
    /* always cleanup */ 
    curl_easy_cleanup(curl);
  }
  curl_global_cleanup();

  return result;
}


/*
 * // Login
 * bool success = callPWG(context, response, "username=sven&password=XpassYwordZ", "https://www.consensionis.de/Fotoprojekte", DT_PIWIGO_API_SESSION_LOGIN);
 *
 * // Logout
 * bool success = callPWG(context, response, "", "https://www.consensionis.de/Fotoprojekte", DT_PIWIGO_API_SESSION_LOGOUT);
 *
 * // Get Category List
 * bool success = callPWG(context, response, "", "https://www.consensionis.de/Fotoprojekte", DT_PIWIGO_API_CATEGORY_GETLIST "&recursive=true&tree_output=true");
 *
 * // Get Category List
 * bool success = callPWG(context, response, "", "https://www.consensionis.de/Fotoprojekte", DT_PIWIGO_API_CATEGORY_ADD "&name=Übungen&parent=0");
 *
 * // Upload Photo
 * bool success = callPWG(context, response, "category=2&name=The Lion King&author=Sven Fröhlich&comment=Der erste Versuch des Foto-Uploads&level=0&tags=Tiere&tags=Tierpark", "https://www.consensionis.de/Fotoprojekte", DT_PIWIGO_API_IMAGES_ADD_SIMPLE);
 *
 * // Status
 * bool success = callPWG(context, response, "", "https://www.consensionis.de/Fotoprojekte", DT_PIWIGO_API_SESSION_GET_STATUS);
 *
 */
static gboolean callPWG(dt_module_imageio_storage_piwigo_context_t *context, GString *response, const char* postdata, const char *method)
{
  CURL *curl = NULL;
  CURLcode res;
  char curl_errbuf[CURL_ERROR_SIZE] = "";
  gboolean result = FALSE;

  if ( !context || !response )
  {
    // shortcut exit
    return result;
  }
  
  curl_global_init(CURL_GLOBAL_ALL);
  curl = curl_easy_init();
  if ( curl )
  {
    /* First set the URL that is about to receive our POST. This URL can
       just as well be a https:// URL if that is what should receive the
       data. */
    char *callBuffer = malloc(PATH_MAX);
    memset(callBuffer, 0, PATH_MAX);
    // TODO - Check for correct length and url injection!!!
    snprintf(callBuffer, PATH_MAX, "%s/%s", context->site, method);
    curl_easy_setopt(curl, CURLOPT_URL, callBuffer);

    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_errbuf);

#ifdef DT_PIWIGO_DEBUG
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 2L);
#else
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);
#endif

    if (!context->auth->sslPeer) {
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, FALSE);
    }

    /* Now specify the POST data */ 
    if ( postdata && strlen(postdata) > 0 ) {
      // TODO - Check postdata for code injection
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postdata);
    }
    
    /* specify function call for writing response */
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_data_callback);

    // Set the response buffer
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
 
    /* Perform the request, res will get the return code */ 
    res = curl_easy_perform(curl);
    /* Check for errors */ 
    if(res == CURLE_OK)
    {
      result = TRUE;
    }
    else
    {
      fprintf(stderr, "curl_easy_perform() failed: %s\n",
              curl_easy_strerror(res));

    }
    // Clean up all buffers
    free(callBuffer);
    curl_easy_cleanup(curl);
  }
  curl_global_cleanup();
  return result;
}


//
//static bool uploadFile(dt_module_imageio_storage_piwigo_context_t *context, const char *displayname, const char * file)
//{
//  CURL *curl = context->curl;
//  CURLcode res;
//
//  struct curl_httppost *formpost = NULL;
//  struct curl_httppost *lastptr = NULL;
//  struct curl_slist *headerlist = NULL;
//  static const char buf[] = "Expect:";
//
//  curl_global_init(CURL_GLOBAL_ALL);
//
//  /* Fill in the file upload field */
//  curl_formadd(&formpost,
//               &lastptr,
//               CURLFORM_COPYNAME, "sendfile",
//               CURLFORM_FILE, file,
//               CURLFORM_END);
//
//  /* Fill in the filename field */
//  curl_formadd(&formpost,
//               &lastptr,
//               CURLFORM_COPYNAME, "filename",
//               CURLFORM_COPYCONTENTS, displayname,
//               CURLFORM_END);
//
//
//  /* Fill in the submit field too, even if this is rarely needed */
//  curl_formadd(&formpost,
//               &lastptr,
//               CURLFORM_COPYNAME, "submit",
//               CURLFORM_COPYCONTENTS, "send",
//               CURLFORM_END);
//
//
//  curl = curl_easy_init();
//  /* initialize custom header list (stating that Expect: 100-continue is not
//     wanted */
//  headerlist = curl_slist_append(headerlist, buf);
//  if(curl) {
//
//      char *callBuffer = malloc(PATH_MAX);
//      memset(callBuffer, 0, PATH_MAX);
//
//      // TODO - Check for correct length and url injection!!!
//      snprintf(callBuffer, PATH_MAX, "%s/%s", gtk_entry_get_text(GTK_ENTRY(context->ui->site)), method->str);
//
//
//
//    /* Now specify the POST data */
//    unsigned int category = 2; // Album "Tiere" (Demo)
//    g_char *name = "DSC0001.jpg";
//    g_char *author = "Sven Fröhlich";
//    g_char *comment = "Das erste Bild, welches per Piwigo-Extension hochgeladen wurde! :-D";
//    unsigned int level = 0;
//    g_char *tags = "Tiere";  // Eigentlich ein Array of char pointers!
//    snprintf(postBuffer, NAME_MAX, "category=%d&name=%s&author=%s&comment=%s&level=%d&tags=%s", category, name, author, comment, level, tags);
//
//    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postBuffer);
//
//    /* what URL that receives this POST */
//    curl_easy_setopt(curl, CURLOPT_URL, gtk_entry_get_text(GTK_ENTRY(context->ui->site)), "pwg.images.addSimple");
//    if((argc == 2) && (!strcmp(argv[1], "noexpectheader")))
//      /* only disable 100-continue header if explicitly requested */
//      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);
//    curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
//
//    /* Perform the request, res will get the return code */
//    res = curl_easy_perform(curl);
//    /* Check for errors */
//    if(res != CURLE_OK)
//      fprintf(stderr, "curl_easy_perform() failed: %s\n",
//              curl_easy_strerror(res));
//
//    /* always cleanup */
//    curl_easy_cleanup(curl);
//
//    /* then cleanup the formpost chain */
//    curl_formfree(formpost);
//    /* free slist */
//    curl_slist_free_all(headerlist);
//  }
//  return TRUE;
//}

/* called once at the end (after exporting all images), if implemented. */
void finalize_store(struct dt_imageio_module_storage_t *self, struct dt_imageio_module_data_t *data)
{
  return;
}

void *legacy_params(struct dt_imageio_module_storage_t *self, const void *const old_params,
                    const size_t old_params_size, const int old_version, const int new_version,
                    size_t *new_size)
{
  return (void *)NULL;
}

size_t params_size(struct dt_imageio_module_storage_t *self)
{
  return 0;
}

void *get_params(struct dt_imageio_module_storage_t *self)
{
  return (void *) self->gui_data;
}

void free_params(struct dt_imageio_module_storage_t *self, struct dt_imageio_module_data_t *data)
{
  return;
}

int set_params(struct dt_imageio_module_storage_t *self, const void *params, const int size)
{
  return DT_PIWIGO_SUCCESS;
}


void export_dispatched(struct dt_imageio_module_storage_t *self)
{
  return;
}


static void clicked_login_button(GtkWidget *widget, dt_imageio_module_storage_t *self)
{
  dt_module_imageio_storage_piwigo_context_t *context = (dt_module_imageio_storage_piwigo_context_t *) self->gui_data;

  GdkCursor * cursor = gdk_cursor_new_from_name(gdk_display_manager_get_default_display(gdk_display_manager_get()),"wait");
  GdkWindow * window = gtk_widget_get_parent_window (context->ui->login_button);
  gdk_window_set_cursor (window, cursor);
  //const gchar *last = gtk_button_get_label(GTK_BUTTON(context->ui->login_button));
  gtk_button_set_label(GTK_BUTTON(context->ui->login_button), _("Wait..."));

  while ( g_main_context_pending(NULL) ) {
    // refresh button label, before continue
    g_main_context_iteration(NULL,FALSE);
  }

  if (!context->auth->registered) {  // logging in
    if (context->site) {
      free(context->site);
      context->site = NULL;
    }
    context->site = strdup(
        gtk_entry_get_text(GTK_ENTRY(context->ui->site)));
    context->auth->registered = pwg_login(context);
  }
  else  {// logging out
    context->auth->registered = !pwg_logout(context); // unregister on logout success
  }

  // Login status change now
  if (context->auth->registered) {
    gtk_button_set_label(GTK_BUTTON(context->ui->login_button),
        _("Logout"));
  } else {

    if (context->site) {
      free(context->site);
      context->site = NULL;
    }
    gtk_button_set_label(GTK_BUTTON(context->ui->login_button), _("Login"));
  }
  // enable or disable login information widgets
  gtk_widget_set_sensitive (context->ui->site, !(context->auth->registered));
  gtk_widget_set_sensitive (context->ui->username, !(context->auth->registered));
  gtk_widget_set_sensitive (context->ui->password, !(context->auth->registered));
  gdk_window_set_cursor (window, NULL);
  
}

// static gboolean callREST(GString *response, dt_module_imageio_storage_piwigo_context_t *context, GString *method) 
// {
  // gboolean result = FALSE;
  // CURL *curl = NULL;
  // CURLcode res = 0;
  // char curl_errbuf[CURL_ERROR_SIZE];
  
  // curl = curl_easy_init();
  // if ( curl )
  // {
    // // Check if we are successful logged in
    // if ( context->auth->registered || pwg_login(context) )
    // {
      // char *callBuffer = malloc(PATH_MAX);
      // memset(callBuffer, 0, PATH_MAX);
    
      // // TODO - Check for correct length and url injection!!!
      // snprintf(callBuffer, PATH_MAX, "%s/%s", gtk_entry_get_text(GTK_ENTRY(context->ui->site)), method->str);
    
      // curl_easy_setopt(curl, CURLOPT_URL, callBuffer);
      // curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_errbuf);
// #ifdef picasa_EXTRA_VERBOSE
      // curl_easy_setopt(curl, CURLOPT_VERBOSE, 2L);
      // curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
// #else
      // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
      // curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
// #endif

      // if ( !context->auth->sslPeer )
      // {
        // curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, FALSE);
      // }

      // /* We don't do POST data */ 
      // curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
 
      // // Set the response data function and buffer  
      // curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_data_callback);
      // curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
 
      // /* Perform the request, res will get the return code */ 
      // res = curl_easy_perform(curl);
    
      // // free temporary buffers as soon as possible
      // free(callBuffer);
        
      // /* Check for errors */ 
      // if(res == CURLE_OK)
      // {
        // result = TRUE;
      // }
      // else
      // {
        // fprintf(stderr, "curl_easy_perform() failed: [%d] %s\n", res, curl_easy_strerror(res));
      // }

      // /* always cleanup */ 
      // curl_easy_cleanup(curl);
    // }
    // curl_global_cleanup();
  // }
  // return result;
// }


static void pwg_debug(const char *format, const char *file, size_t line, ...) {
#ifdef DT_PIWIGO_DEBUG
	char full_format[2048];
	va_list arglist;
	va_start( arglist, line );
	snprintf(full_format, 2048, "DEBUG[%s:%lu]: %s\n", file, line, format);
	printf("\nFORMAT: %s", full_format);

	printf(  "DATA  : %s\n", va_arg(arglist, char*));
    printf(full_format, arglist);
    va_end( arglist );
#endif
	return;
}

/*
 * Returns the ID of the last category in the checked/created category path.
 * -1 if it could'nt be created.
 * categoryPath has to be in format:
 * "[/]rootCategory/subCategory_1/Category\/with\/slashes/leafCategory"
 * leading slash is optional
 */
static int pwg_createCategoryPath(dt_module_imageio_storage_piwigo_context_t *context, const char * path) {
  int id = -1;
  GString *response = g_string_new("");
  GString *targetsArray[255];
  size_t targetsPosition = 0;
  GString *target = g_string_new("");
  gchar * escapedPath = g_strescape(path, NULL);
  size_t pathPosition = 0;

  printf("Path: '%s'\n", path);
  printf("[DEBUG - %s:%d] Initial target category      : '%s'\n", __FILE__, __LINE__, path);
  printf("[DEBUG - %s:%d] Searching for target category: '%s'\n", __FILE__, __LINE__, escapedPath);

  size_t targetStart = pathPosition;
  while ( pathPosition < strlen(escapedPath) ) {
	  printf(".");
    if ( '/' == escapedPath[pathPosition] ) {
      if (pathPosition > targetStart) {
        // add target to array
    	  printf("Target found: '%s'\n", target->str);
        targetsArray[targetsPosition] = g_string_new(target->str);
        targetsPosition++;
        g_string_truncate(target,0);
      }
    } 
    else {
      if ( '\\' == escapedPath[pathPosition] ) {
        // jump over escaped characters
        pathPosition++;
      }
      // append char to target category
      target = g_string_append_c (target, escapedPath[pathPosition]);
      printf("Target: '%s'\n", target->str);
    }
    pathPosition++;
  }
  printf("\n");
  // targets Array should be filled - Now look what we already have and what needs to be created
  
  if ( callPWG(context, response, NULL, DT_PIWIGO_API_CATEGORY_GET_ADMINLIST) ) {
    pwg_debug("Calling Json Parser:\n%s\n---\n", __FILE__, __LINE__, response->str);

    //JsonObject *rootObject = parse_json_response(context, response);
    JsonNode *rootNode = json_parser_get_root(context->parser);
    
    if ( rootNode ) {
      size_t catId = 0;

      for ( int i = 0; i <= targetsPosition; i++ ) {
        char expression[2048];
        int count = 0;
        
        printf("DEBUG: [%s:%d] Target-%02d : '%s'\n", __FILE__, __LINE__, i, (targetsArray[i])->str); 
        
        /* JSON path query
        $.result.categories[?(@ && /Exkursionen/.test(@.name) )].id
        => [ "7" ]
        
        $.result.categories[?(@ && /Norwegen/.test(@.name) && /\b7\b/.test(@.uppercats) )]
        => 
        [
            {
                "id": "9",
                "name": "Norwegen",
                "comment": "",
                "uppercats": "7,9",
                "global_rank": "1.1",
                "dir": null,
                "status": "private",
                "nb_images": "1",
                "fullname": "Exkursionen / Norwegen"
            }
        ]
        */
        
        //pwg_debug("Target-%02d : '%s'\n", __FILE__, __LINE__, i, (targetsArray[i])->str); 

        if ( catId > 0 ) {
        	count = sprintf(expression, "$.result.categories[?(@.id_uppercat==%s) and ?(@.name==%s].id", "null", (targetsArray[i])->str);
        }
        else {
        	count = sprintf(expression, "$.result.categories[?(@.id_uppercat==%lu) and ?(@.name==%s].id", catId, (targetsArray[i])->str);
        }
        if ( count > 0 && count < 2048 ) {
          JsonNode * catResult = json_path_query(expression, rootNode, NULL);
          
          catId = json_node_get_int(catResult);
          
          pwg_debug("Target-%02d : '%s'\n", __FILE__, __LINE__, i, (targetsArray[i])->str); 
        }
      }
    }
  }

//  if ( callPWG(context, response, NULL, DT_PIWIGO_API_CATEGORY_GETLIST PWG_GETLIST_RECURSIVE PWG_GETLIST_TREE) )
//  {
//
//    // api call succeeded, now parse response
//#ifdef DT_PIWIGO_DEBUG
//    g_printf("Calling Json Parser:\n%s\n---\n", response->str);
//#endif
//    JsonObject *rootObject = parse_json_response(context, response);
//    if ( rootObject ) {
//      char catId[10] = "null";  // holds the parent category as string value
//      char expression[2048];
//      int count = sprintf(expression, "$.categories[?(@.id_uppercat==%s)]", catId);
//      if ( count > 0 && count < 2048 ) {
//        JsonNode * catResult = json_path_query(expression, json_object_get_member(rootObject,"result"));
//
//
//      }
//
//      if ( 0 == strcmp("ok", json_object_get_string_member(rootObject, "stat") ) ) {
//        JsonArray result = json_object_get_array_member(rootObject, "result");
//        GList * resultList = json_array_get_elements(result);
//        GList * currentElement = resultList;
//        unsigned int parentId = 0;
//
//
//
//        while ( currentElement ) {
//          JsonNode *currentNode = (JsonNode *) currentElement->data;
//
//          /* Do something with @currentNode. */
//          JsonObject categoryObject = json_node_get_object(currentNode);
//          GString *rootCategory = g_string_new(json_object_get_string_member(categoryObject, "name"));
//#ifdef DT_PIWIGO_DEBUG
//          g_printf("Found root category        : %s\n", rootCategory);
//#endif
//
//        }
//
//          //@TODO: expand all darktable variables
//          //gchar *result_filename = dt_variables_expand(d->vp, pattern, TRUE);
//
//          // get next list element
//          currentElement = currentElement->next;
//        }
//
//      }
//    }
//  }
  
  return id;
}

static gboolean pwg_login(dt_module_imageio_storage_piwigo_context_t *context)
{
  //CURL *curl;
//  char curl_errbuf[CURL_ERROR_SIZE];
//  CURLcode res;
  GString *response = g_string_new("");
  gboolean result = FALSE;
  char *postbuffer = malloc(NAME_MAX);
  
  /* specify the POST data */
  memset(postbuffer, 0, NAME_MAX);
  snprintf(postbuffer, NAME_MAX, "username=%s&password=%s", gtk_entry_get_text(GTK_ENTRY(context->ui->username)), gtk_entry_get_text(GTK_ENTRY(context->ui->password)));
 
  if ( callPWG(context, response, postbuffer, DT_PIWIGO_API_SESSION_LOGIN) )
  {
    // api call succeeded, now parse response
#ifdef DT_PIWIGO_DEBUG
    g_printf("Calling Json Parser:\n%s\n---\n", response->str);
#endif
    JsonObject *rootObject = parse_json_response(context, response);
    if ( rootObject ) {
      if ( 0 == strcmp("ok", json_object_get_string_member(rootObject, "stat") ) ) {
        result = json_object_get_boolean_member(rootObject, "result");
        // clear password field
        gtk_entry_set_text(GTK_ENTRY(context->ui->password), "");
      }
    }
  }

  // free temporary buffers as soon as possible
  free(postbuffer);
  g_string_free(response, TRUE);

#ifdef DT_PIWIGO_DEBUG
    g_printf("Result of pwg_login(): %s\n",  result ? "true" : "false");
#endif
  return result;
}


static gboolean pwg_logout(dt_module_imageio_storage_piwigo_context_t *context)
{
 // CURL *curl;
//  char curl_errbuf[CURL_ERROR_SIZE];
 // CURLcode res;
  GString *response = g_string_new("");
  gboolean result = FALSE;

  result = callPWG(context, response, NULL, DT_PIWIGO_API_SESSION_LOGOUT);
    if ( result )
  {
    // api call succeeded, now parse response
#ifdef DT_PIWIGO_DEBUG    
    g_printf("Calling Json Parser:\n%s\n---\n", response->str);
#endif
      JsonObject *rootObject = parse_json_response(context, response);
      if ( 0 == strcmp("ok", json_object_get_string_member(rootObject, "stat") ) ) {
        result = json_object_get_boolean_member(rootObject, "result");
      }
  }
  
  g_string_free(response, TRUE);
  return result;
}

static void config_changed_callback(GtkEntry *entry, gpointer user_data)
{
  dt_module_imageio_storage_piwigo_context_t *context = (dt_module_imageio_storage_piwigo_context_t *) ((dt_imageio_module_storage_t *) user_data)->gui_data;
  
  if ( GTK_WIDGET(entry) == context->ui->album )
  {
    dt_conf_set_string("plugins/imageio/storage/piwigo/album", gtk_entry_get_text(entry));
    if ( !context->path ) {
    	context->path = (char *) malloc(PATH_MAX * sizeof(char));
    }
    snprintf(context->path, PATH_MAX, "%s", gtk_entry_get_text(entry));
  }
  else if ( GTK_WIDGET(entry) == context->ui->site )
  {
    dt_conf_set_string("plugins/imageio/storage/piwigo/site", gtk_entry_get_text(entry));
  }
  else if ( GTK_WIDGET(entry) == context->ui->username )
  {
    dt_conf_set_string("plugins/imageio/storage/piwigo/username", gtk_entry_get_text(entry));
  }
  
  return;
}

static size_t curl_write_data_callback(void *ptr, size_t size, size_t nmemb, void *data)
{
  GString *string = (GString *)data;
  g_string_append_len(string, ptr, size * nmemb);
#ifdef DT_PIWIGO_DEBUG
  g_printf("server reply: %s\n", string->str);
#endif
  return size * nmemb;
}

/* static char* getMethodString(dt_piwigo_methods_t method) */
/* { */
/*   switch (methodName)  */
/*   { */
/*     case getStatus: */
/*       snprintf(&methodName, DT_PIWIGO_METHOD_MAXLEN, "%s.getSession", DT_PIWIGO_SESSION); */
/*     case login: */
/*       snprintf(&methodName, DT_PIWIGO_METHOD_MAXLEN, "%s.login", DT_PIWIGO_SESSION); */
/*     case logout: */
/*       snprintf(&methodName, DT_PIWIGO_METHOD_MAXLEN, "%s.logout", DT_PIWIGO_SESSION); */
/*  case getCategories: */
/*       snprintf(&methodName, DT_PIWIGO_METHOD_MAXLEN, "%s.getList", DT_PIWIGO_CATEGORIES); */
/*  case addImages: */
/*    snprintf(&methodName, DT_PIWIGO_METHOD_MAXLEN, "%s.addSimple", DT_PIWIGO_IMAGES); */
/*     default:   // none */
/*       snprintf(&methodName, DT_PIWIGO_METHOD_MAXLEN, ""); */
/*   } */
/*   return methodName; */
/* } */


static JsonObject *parse_json_response(dt_module_imageio_storage_piwigo_context_t *context, GString *response)
{
  //GError error;
  //GError *perror = &error;
  gboolean ret = 0;
  JsonObject *rootObject = NULL;

  g_printf("Load json from data buffer with parser: %p\n", context->parser);
   
  ret = json_parser_load_from_data(context->parser, response->str, -1, NULL);

  g_printf("Result of json_parser_load_from_data(): %d \n", ret);
  if ( ret )
  {
    JsonNode *root = json_parser_get_root(context->parser);
    rootObject = json_node_get_object(root);

    const gchar * stat = json_object_get_string_member(rootObject, "stat");
    if ( !stat || 0 != strcmp(stat,"ok") ) {
      // TODO error and debug handling
      return rootObject = NULL;
    }
  }
  return rootObject;
}


// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
