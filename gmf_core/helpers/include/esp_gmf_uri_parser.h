/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2025 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Structure representing a parsed URI
 *
 *         This structure holds different components of a URI such as scheme, userinfo,
 *         host, port, path, query, etc. Each field corresponds to a specific part of the URI
 */
typedef struct {
    char *scheme;    /*!< The scheme of the URI (e.g., "http", "https") */
    char *userinfo;  /*!< The userinfo component (optional, may include username and password) */
    char *username;  /*!< The username part of the userinfo, if present */
    char *password;  /*!< The password part of the userinfo, if present */
    char *host;      /*!< The host (e.g., domain name or IP address) */
    int   port;      /*!< The port number (optional, if not specified, defaults to scheme's standard port) */
    char *path;      /*!< The path component of the URI */
    char *query;     /*!< The query string of the URI (optional, starts after '?' in URL) */
    char *fragment;  /*!< The fragment identifier (optional, starts after '#' in URL) */
} esp_gmf_uri_t;

/**
 * @brief  Parses a URI string and populates a esp_gmf_uri_t structure with its components
 *
 *         This function takes a URI string as input, parses it, and allocates and populates a
 *         `esp_gmf_uri_t` structure with the parsed components. The caller is responsible for
 *         freeing the allocated memory using `esp_gmf_uri_free()`.
 *
 * @param[in]   uri_str  The URI string to be parsed
 * @param[out]  uri_out  Pointer to the `esp_gmf_uri_t` structure where the parsed URI components will be stored
 *
 * @return
 *       - 0         On success
 *       - Non-zero  Error code on failure
 */
int esp_gmf_uri_parse(const char *uri_str, esp_gmf_uri_t **uri_out);

/**
 * @brief  Frees the memory allocated for a esp_gmf_uri_t object
 *
 *         This function releases all dynamically allocated memory associated with the URI components
 *         within the given `esp_gmf_uri_t` structure. After calling this function, the URI pointer will be invalid.
 *
 * @param[in]  uri  Pointer to the `esp_gmf_uri_t` object to be freed
 */
void esp_gmf_uri_free(esp_gmf_uri_t *uri);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
