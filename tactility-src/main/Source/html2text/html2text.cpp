#include "html2text.h"
#include <cstring>
#include <cctype>
#include <cstdlib>

// Local implementation of strncmp since it's not exported for now
static int local_strncmp(const char* s1, const char* s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) {
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        }
        if (s1[i] == '\0') {
            return 0;
        }
    }
    return 0;
}

enum { HTML_FIRST, HTML_MID };

static int SearchHtmlTag(const char* html, int offset, int* condition) {
    int i = offset;
    size_t max = strlen(html);

    if (offset >= (int)max) return -2;

    switch (*condition) {
        case HTML_FIRST:
            while (i <= (int)max) {
                if (local_strncmp(html + i, "<", 1) == 0) {
                    while (1) {
                        i++;
                        if (local_strncmp(html + i, ">", 1) == 0) {
                            i++;
                            if (local_strncmp(html + i, "<", 1) == 0) {
                                return SearchHtmlTag(html, i, condition);
                            } else {
                                return i;
                            }
                        }
                        if (i == (int)max) {
                            *condition = HTML_MID;
                            return -1;
                        }
                    }
                } else {
                    i++;
                    if (i >= (int)max) return -3;
                }
            }
            break;
        case HTML_MID:
            while (1) {
                if (local_strncmp(html + i, ">", 1) == 0) {
                    i++;
                    if (local_strncmp(html + i, "<", 1) == 0) {
                        return SearchHtmlTag(html, i, condition);
                    } else {
                        return i;
                    }
                }
                i++;
            }
            break;
    }
    return -3;
}

// C-style implementation that returns allocated string
char* html2text_c(const char* html) {
    if (!html) return nullptr;
    
    size_t html_len = strlen(html);
    // Allocate result buffer (worst case: same size as input + null terminator)
    char* result = (char*)malloc(html_len + 1);
    if (!result) return nullptr;
    
    int result_pos = 0;
    int i = 0;
    int condition = HTML_FIRST;
    char tmp[100];
    
    while (i < (int)html_len) {
        if (local_strncmp(html + i, "<", 1) != 0) {
            int first_place = i;
            while (i < (int)html_len && local_strncmp(html + i, " ", 1) != 0 && local_strncmp(html + i, "<", 1) != 0) {
                i++;
            }

            int word_length = i - first_place;
            if (word_length > 0 && word_length < 100) {
                int last_place = i;
                // Skip non-alphanumeric at start/end
                while (first_place < last_place && !isalnum(html[first_place])) first_place++;
                while (last_place > first_place && !isalnum(html[last_place - 1])) last_place--;

                word_length = last_place - first_place;
                if (word_length > 0) {
                    strncpy(tmp, html + first_place, word_length);
                    tmp[word_length] = '\0';
                    // Convert first letter to lowercase if uppercase
                    if (isupper(tmp[0]) && isalpha(tmp[0])) tmp[0] = tolower(tmp[0]);
                    
                    // Copy word to result
                    strcpy(result + result_pos, tmp);
                    result_pos += word_length;
                    
                    // Add space
                    result[result_pos++] = ' ';
                }
            }
            if (i < (int)html_len && local_strncmp(html + i, "<", 1) != 0) i++;
        } else {
            i = SearchHtmlTag(html, i, &condition);
            if (i == -1 || i < -1) break;
        }
    }

    // Remove trailing space
    if (result_pos > 0 && result[result_pos - 1] == ' ') {
        result_pos--;
    }
    
    result[result_pos] = '\0';
    return result;
}

// Wrapper that maintains the std::string interface for compatibility
std::string html2text(const std::string& html) {
    char* c_result = html2text_c(html.c_str());
    if (!c_result) {
        return std::string();
    }
    
    std::string result(c_result);
    free(c_result);
    return result;
}

