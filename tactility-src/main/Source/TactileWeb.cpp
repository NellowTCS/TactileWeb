#include <lvgl.h>

#include <tt_hal.h>
#include <tt_app.h>
#include <tt_kernel.h>
#include <tt_lvgl.h>
#include <tt_lvgl_toolbar.h>
#include <tt_lvgl_keyboard.h>
#include <tt_lock.h>
#include <tt_preferences.h>
#include <tt_wifi.h>

#include <esp_log.h>
#include <esp_http_client.h>
#include <cmath>
#include <cstring>
#include <string>

#include "html2text/html2text.h"

constexpr auto *TAG = "TactileWeb";

// Helper to get toolbar height based on UI scale
static int getToolbarHeight(UiScale uiScale) {
    if (uiScale == UiScaleSmallest) {
        return 22;
    } else {
        return 40;
    }
}

// Global state variables
static lv_obj_t *toolbar = nullptr;
static lv_obj_t *url_input = nullptr;
static lv_obj_t *text_area = nullptr;
static lv_obj_t *text_container = nullptr;
static lv_obj_t *wifi_button = nullptr;
static lv_obj_t *wifi_card = nullptr;
static lv_obj_t *loading_label = nullptr;
static lv_obj_t *retry_button = nullptr;
static lv_obj_t *status_label = nullptr;

static AppHandle app_handle = nullptr;
static std::string last_url;
static std::string initial_url;
static bool is_loading = false;

// Forward declarations
static void fetchAndDisplay(const char* url);
static void showWifiPrompt();
static void updateStatusLabel(const char* text, lv_palette_t color = LV_PALETTE_NONE);

static bool is_wifi_connected() {
    WifiRadioState state = tt_wifi_get_radio_state();
    return state == WifiRadioStateConnectionActive;
}

// UI Event Handlers
static void url_input_cb(lv_event_t* e) {
    const char* url = lv_textarea_get_text(static_cast<const lv_obj_t*>(lv_event_get_target(e)));
    
    if (url && strlen(url) > 0) {
        fetchAndDisplay(url);
        tt_lvgl_software_keyboard_hide();
    }
}

static void wifi_connect_cb(lv_event_t* e) {
    tt_app_start("WifiManage");
}

static void retry_cb(lv_event_t* e) {
    if (!last_url.empty()) {
        fetchAndDisplay(last_url.c_str());
    }
}

static void focus_url_cb(lv_event_t* e) {
    if (url_input) {
        lv_obj_add_state(url_input, LV_STATE_FOCUSED);
        // TODO: not in tt_init
        // lv_obj_scroll_to_view(url_input, LV_ANIM_ON);
    }
}

static void clear_cb(lv_event_t* e) {
    if (text_area) {
        lv_textarea_set_text(text_area, "");
    }
}

// URL and content management
static void loadLastUrl() {
    PreferencesHandle prefs = tt_preferences_alloc("tactileweb");
    initial_url = "http://example.com";
    
    // Allocate buffer for potential URL
    char url_buffer[256];
    if (tt_preferences_opt_string(prefs, "last_url", url_buffer, sizeof(url_buffer))) {
        initial_url = url_buffer;
    }
    
    last_url = initial_url;
    tt_preferences_free(prefs);
}

static void saveLastUrl(const char* url) {
    if (url && strlen(url) > 0) {
        last_url = url;
        PreferencesHandle prefs = tt_preferences_alloc("tactileweb");
        tt_preferences_put_string(prefs, "last_url", last_url.c_str());
        tt_preferences_free(prefs);
    }
}

static bool isValidUrl(const char* url) {
    if (!url || strlen(url) < 7) return false;
    return (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0);
}

// UI State Management
static void updateStatusLabel(const char* text, lv_palette_t color) {
    if (!status_label && toolbar) {
        status_label = lv_label_create(toolbar);
        lv_obj_align(status_label, LV_ALIGN_LEFT_MID, 10, 0);
    }
    
    if (status_label) {
        lv_label_set_text(status_label, text);
        if (color != LV_PALETTE_NONE) {
            lv_obj_set_style_text_color(status_label, lv_palette_main(color), 0);
        }
    }
}

static void clearContent() {
    if (retry_button) {
        lv_obj_delete(retry_button);
        retry_button = nullptr;
    }
    if (wifi_card) {
        lv_obj_delete(wifi_card);
        wifi_card = nullptr;
        wifi_button = nullptr; // wifi_button is a child of wifi_card, so it's deleted too
    }
}

static void clearLoading() {
    is_loading = false;
    if (loading_label) {
        lv_obj_delete(loading_label);
        loading_label = nullptr;
    }
}

static void showWifiPrompt() {
    clearContent();
    clearLoading();

    lv_textarea_set_text(text_area, "");

    // Get display metrics for responsive sizing
    lv_coord_t width = lv_obj_get_width(text_container);
    bool is_small = (width < 240);

    // Create a card-style container for the WiFi prompt
    wifi_card = lv_obj_create(text_area);
    lv_obj_set_size(wifi_card, LV_PCT(90), LV_SIZE_CONTENT);
    lv_obj_center(wifi_card);
    lv_obj_set_style_radius(wifi_card, is_small ? 8 : 16, 0);
    lv_obj_set_style_bg_color(wifi_card, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(wifi_card, LV_OPA_10, 0);
    lv_obj_set_style_border_width(wifi_card, 1, 0);
    lv_obj_set_style_border_color(wifi_card, lv_color_hex(0x666666), 0);
    lv_obj_set_style_border_opa(wifi_card, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(wifi_card, is_small ? 12 : 20, 0);
    
    // WiFi icon
    lv_obj_t* icon = lv_label_create(wifi_card);
    lv_label_set_text(icon, LV_SYMBOL_WIFI);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_text_font(icon, lv_font_get_default(), 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xFF9500), 0);

    // Title
    lv_obj_t* wifi_label = lv_label_create(wifi_card);
    lv_label_set_text(wifi_label, "No Wi-Fi Connection");
    lv_obj_align_to(wifi_label, icon, LV_ALIGN_OUT_BOTTOM_MID, 0, is_small ? 8 : 12);
    lv_obj_set_style_text_font(wifi_label, lv_font_get_default(), 0);
    lv_obj_set_style_text_align(wifi_label, LV_TEXT_ALIGN_CENTER, 0);

    // Subtitle
    lv_obj_t* subtitle = lv_label_create(wifi_card);
    lv_label_set_text(subtitle, "Connect to Wi-Fi to browse the web");
    lv_obj_align_to(subtitle, wifi_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
    lv_obj_set_style_text_font(subtitle, lv_font_get_default(), 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_align(subtitle, LV_TEXT_ALIGN_CENTER, 0);

    // Connect button
    wifi_button = lv_btn_create(wifi_card);
    lv_obj_set_size(wifi_button, LV_PCT(80), is_small ? 28 : 36);
    lv_obj_align_to(wifi_button, subtitle, LV_ALIGN_OUT_BOTTOM_MID, 0, is_small ? 12 : 16);
    lv_obj_set_style_radius(wifi_button, is_small ? 6 : 8, 0);
    lv_obj_set_style_bg_color(wifi_button, lv_color_hex(0x007BFF), 0);

    lv_obj_t* btn_label = lv_label_create(wifi_button);
    lv_label_set_text(btn_label, "Connect to Wi-Fi");
    lv_obj_center(btn_label);
    lv_obj_set_style_text_font(btn_label, lv_font_get_default(), 0);
    lv_obj_set_style_text_color(btn_label, lv_color_hex(0xFFFFFF), 0);

    lv_obj_add_event_cb(wifi_button, wifi_connect_cb, LV_EVENT_CLICKED, nullptr);

    updateStatusLabel("No WiFi Connection", LV_PALETTE_RED);
}

static void showLoading(const char* url = nullptr) {
    if (is_loading) return;
    
    is_loading = true;
    clearContent();
    
    loading_label = lv_label_create(text_area);
    if (url) {
        std::string loading_text = "Loading: ";
        loading_text += url;
        lv_label_set_text(loading_label, loading_text.c_str());
    } else {
        lv_label_set_text(loading_label, "Loading...");
    }
    lv_obj_center(loading_label);
    lv_obj_set_style_text_align(loading_label, LV_TEXT_ALIGN_CENTER, 0);
    
    updateStatusLabel("Loading...", LV_PALETTE_YELLOW);
}

static void showRetryButton() {
    if (!retry_button && text_area) {
        retry_button = lv_btn_create(text_area);
        lv_obj_set_size(retry_button, 100, 35);
        lv_obj_t* btn_label = lv_label_create(retry_button);
        lv_label_set_text(btn_label, "Retry");
        lv_obj_center(btn_label);
        lv_obj_align(retry_button, LV_ALIGN_BOTTOM_MID, 0, -20);
        lv_obj_add_event_cb(retry_button, retry_cb, LV_EVENT_CLICKED, nullptr);
    }
}

static void showError(const char* error_msg, const char* url = nullptr) {
    clearLoading();
    clearContent();
    
    std::string full_error = "Error: ";
    full_error += error_msg;
    
    lv_textarea_set_text(text_area, full_error.c_str());
    
    if (url && strlen(url) > 0) {
        showRetryButton();
    }
    
    updateStatusLabel("Error", LV_PALETTE_RED);
}

static void fetchAndDisplay(const char* url) {
    if (!url || strlen(url) == 0) {
        showError("Invalid URL provided");
        return;
    }

    if (!is_wifi_connected()) {
        showWifiPrompt();
        return;
    }

    if (!isValidUrl(url)) {
        showError("Invalid URL format. Please use http:// or https://");
        return;
    }

    showLoading(url);
    lv_textarea_set_text(text_area, "");

    // Configure HTTP client
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 10000;
    config.skip_cert_common_name_check = true;
    config.buffer_size = 4096;
    config.buffer_size_tx = 1024;
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        showError("Failed to initialize HTTP client", url);
        return;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        showError("Failed to connect to server", url);
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return;
    }

    // Get response code
    int status_code = esp_http_client_get_status_code(client);
    if (status_code < 200 || status_code >= 300) {
        std::string error_msg = "HTTP Error: " + std::to_string(status_code);
        showError(error_msg.c_str(), url);
        esp_http_client_cleanup(client);
        return;
    }

    int content_length = esp_http_client_fetch_headers(client);
    ESP_LOGI(TAG, "Content length: %d, Status: %d", content_length, status_code);

    // Read content in chunks
    std::string html_content;
    char buffer[2048];
    int total_read = 0;
    const int max_content_size = 32768; // 32KB limit

    while (total_read < max_content_size) {
        int len = esp_http_client_read(client, buffer, sizeof(buffer) - 1);
        if (len <= 0) break;
        
        buffer[len] = '\0';
        html_content.append(buffer, len);
        total_read += len;
        
        // Update loading progress
        if (loading_label) {
            std::string progress = "Loading... (" + std::to_string(total_read) + " bytes)";
            lv_label_set_text(loading_label, progress.c_str());
        }
    }

    esp_http_client_cleanup(client);

    if (html_content.empty()) {
        showError("No content received from server", url);
        return;
    }

    // Convert HTML to plain text
    std::string plain_text = html2text(html_content);
    if (plain_text.empty()) {
        plain_text = html_content; // Fallback to raw HTML
    }

    if (plain_text.empty()) {
        plain_text = "Content received but could not be processed.";
    }

    // Limit text length for display
    if (plain_text.length() > 8192) {
        plain_text = plain_text.substr(0, 8192) + "\n\n[Content truncated...]";
    }

    clearLoading();
    clearContent();
    lv_textarea_set_text(text_area, plain_text.c_str());
    
    // Scroll to top
    lv_obj_scroll_to_y(text_area, 0, LV_ANIM_ON);
    
    saveLastUrl(url);
    updateStatusLabel("Content Loaded", LV_PALETTE_GREEN);
    
    ESP_LOGI(TAG, "Successfully loaded content from %s (%d bytes)", 
            url, static_cast<int>(plain_text.length()));
}

// C callback functions
extern "C" void onShow(void *app, void *data, lv_obj_t *parent) {
    app_handle = app;

    // Get UI scale and calculate layout
    UiScale uiScale = tt_hal_configuration_get_ui_scale();
    int toolbar_height = getToolbarHeight(uiScale);

    // Create toolbar with additional buttons
    toolbar = tt_lvgl_toolbar_create_for_app(parent, app_handle);
    lv_obj_align(toolbar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_scroll_dir(toolbar, LV_DIR_NONE);

    // URL focus button
    lv_obj_t* focus_btn = lv_btn_create(toolbar);
    lv_obj_set_size(focus_btn, 80, 30);
    lv_obj_t* focus_label = lv_label_create(focus_btn);
    lv_label_set_text(focus_label, "URL");
    lv_obj_center(focus_label);
    lv_obj_align(focus_btn, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_add_event_cb(focus_btn, focus_url_cb, LV_EVENT_CLICKED, nullptr);

    // Clear button
    lv_obj_t* clear_btn = lv_btn_create(toolbar);
    lv_obj_set_size(clear_btn, 60, 30);
    lv_obj_t* clear_label = lv_label_create(clear_btn);
    lv_label_set_text(clear_label, "Clear");
    lv_obj_center(clear_label);
    lv_obj_align_to(clear_btn, focus_btn, LV_ALIGN_OUT_LEFT_MID, -5, 0);
    lv_obj_add_event_cb(clear_btn, clear_cb, LV_EVENT_CLICKED, nullptr);

    // URL input field
    url_input = lv_textarea_create(parent);
    lv_obj_set_size(url_input, LV_HOR_RES - 40, 35);
    lv_obj_align_to(url_input, toolbar, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_textarea_set_placeholder_text(url_input, "Enter URL (e.g., http://example.com)");
    lv_textarea_set_one_line(url_input, true);
    lv_obj_add_event_cb(url_input, url_input_cb, LV_EVENT_READY, nullptr);
    // TODO: not in tt_init
    // lv_obj_set_scrollbar_mode(url_input, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(url_input, LV_DIR_NONE);

    // Content container
    lv_coord_t parent_width = lv_obj_get_width(parent);
    lv_coord_t parent_height = lv_obj_get_height(parent);
    lv_coord_t container_height = parent_height - toolbar_height - 50; // Account for toolbar and URL input
    
    text_container = lv_obj_create(parent);
    lv_obj_set_size(text_container, parent_width - 20, container_height);
    lv_obj_align_to(text_container, url_input, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    // TODO: not in tt_init
    // lv_obj_set_scrollbar_mode(text_container, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_border_width(text_container, 1, 0);
    lv_obj_set_style_border_color(text_container, lv_palette_main(LV_PALETTE_GREY), 0);

    // Text area for content display
    text_area = lv_textarea_create(text_container);
    lv_obj_set_size(text_area, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(text_area, 0, 0);
    // TODO: not in tt_init
    // lv_obj_set_scrollbar_mode(text_area, LV_SCROLLBAR_MODE_AUTO);
    lv_textarea_set_text(text_area, "Enter a URL above to browse the web.");
    
    // Load saved settings
    loadLastUrl();
    lv_textarea_set_text(url_input, initial_url.c_str());

    // Initial state check
    if (!is_wifi_connected()) {
        showWifiPrompt();
    } else {
        updateStatusLabel("WiFi Connected", LV_PALETTE_GREEN);
        if (!last_url.empty() && last_url != initial_url) {
            // Auto-load last URL if it's different from default
            fetchAndDisplay(last_url.c_str());
        }
    }
}

extern "C" void onHide(void *app, void *data) {
    // Reset state
    is_loading = false;
    app_handle = nullptr;
    
    // Clear object pointers
    toolbar = nullptr;
    url_input = nullptr;
    text_area = nullptr;
    text_container = nullptr;
    wifi_button = nullptr;
    wifi_card = nullptr;
    loading_label = nullptr;
    retry_button = nullptr;
    status_label = nullptr;
}

AppRegistration manifest = {
    .createData = nullptr,
    .destroyData = nullptr,
    .onCreate = nullptr,
    .onDestroy = nullptr,
    .onShow = onShow,
    .onHide = onHide,
    .onResult = nullptr,
};

extern "C" void app_main(void) { 
    tt_app_register(manifest); 
}
