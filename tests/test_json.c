/* Kiểm thử bộ JSON phẳng (SDD §3.3) dùng chung cho handler của M2/M3. */
#include <limits.h>
#include <string.h>

#include "common/fsp_json.h"
#include "test_util.h"

static int test_get_string(void) {
    const char *j = "{\"token\":\"ab12\", \"path\" : \"/home/an/Báo cáo\",\"n\":3}";
    char out[64];
    CHECK(fsp_json_get_str(j, "token", out, sizeof(out)) == 0 && strcmp(out, "ab12") == 0,
          "simple string");
    CHECK(
        fsp_json_get_str(j, "path", out, sizeof(out)) == 0 && strcmp(out, "/home/an/Báo cáo") == 0,
        "UTF-8 passes through, whitespace tolerated");
    CHECK(fsp_json_get_str(j, "missing", out, sizeof(out)) == -1 && out[0] == '\0',
          "missing key -> -1, out emptied");
    CHECK(fsp_json_get_str(j, "n", out, sizeof(out)) == -1, "number is not a string");

    const char *esc = "{\"s\":\"q\\\"b\\\\n\\n t\\t \\u00e9 \\ud83d\\ude00 \\/\"}";
    CHECK(fsp_json_get_str(esc, "s", out, sizeof(out)) == 0, "escapes decode");
    CHECK(strcmp(out, "q\"b\\n\n t\t \xC3\xA9 \xF0\x9F\x98\x80 /") == 0,
          "\\\" \\\\ \\n \\t \\u00e9 surrogate pair \\/");

    char small[5];
    CHECK(fsp_json_get_str(j, "path", small, sizeof(small)) == -2, "too small -> -2");
    CHECK(strlen(small) < sizeof(small), "still NUL-terminated");
    return 0;
}

static int test_key_matching_is_structural(void) {
    /* Chuỗi "token" xuất hiện BÊN TRONG một giá trị không được nhận nhầm là key */
    const char *j = "{\"note\":\"\\\"token\\\":\\\"fake\\\"\",\"token\":\"real\"}";
    char out[32];
    CHECK(fsp_json_get_str(j, "token", out, sizeof(out)) == 0 && strcmp(out, "real") == 0,
          "only real keys match");
    CHECK(fsp_json_has(j, "fake") == 0, "value text is not a key");
    /* Key trùng: lấy lần xuất hiện đầu tiên */
    CHECK(fsp_json_get_str("{\"a\":\"1\",\"a\":\"2\"}", "a", out, sizeof(out)) == 0 &&
              strcmp(out, "1") == 0,
          "first duplicate wins");
    return 0;
}

static int test_int_and_bool(void) {
    const char *j =
        "{\"size\":1048576,\"neg\":-42,\"f\":1.5,\"e\":1e3,\"big\":"
        "99999999999999999999,\"r\":true,\"w\":false,\"s\":\"7\",\"z\":null}";
    long long v = 0;
    bool b = false;
    CHECK(fsp_json_get_int(j, "size", &v) == 0 && v == 1048576, "positive int");
    CHECK(fsp_json_get_int(j, "neg", &v) == 0 && v == -42, "negative int");
    CHECK(fsp_json_get_int(j, "f", &v) == -1, "fraction rejected");
    CHECK(fsp_json_get_int(j, "e", &v) == -1, "exponent rejected");
    CHECK(fsp_json_get_int(j, "big", &v) == -1, "overflow rejected");
    CHECK(fsp_json_get_int(j, "s", &v) == -1, "string is not int");
    CHECK(fsp_json_get_bool(j, "r", &b) == 0 && b == true, "true");
    CHECK(fsp_json_get_bool(j, "w", &b) == 0 && b == false, "false");
    CHECK(fsp_json_get_bool(j, "s", &b) == -1, "string is not bool");
    CHECK(fsp_json_has(j, "z") == 1, "null key exists");
    CHECK(fsp_json_get_int(j, "z", &v) == -1, "null is not int");

    char maxs[64];
    snprintf(maxs, sizeof(maxs), "{\"m\":%lld}", LLONG_MAX);
    CHECK(fsp_json_get_int(maxs, "m", &v) == 0 && v == LLONG_MAX, "LLONG_MAX");
    return 0;
}

static int test_malformed_rejected(void) {
    const char *bad[] = {
        "",
        "[1,2]",
        "{\"a\":{\"b\":1}}", /* lồng: không phải JSON phẳng */
        "{\"a\":[1]}",       /* mảng */
        "{\"a\":1",          /* thiếu } */
        "{\"a\":1,}",        /* dấu phẩy thừa */
        "{\"a\" 1}",         /* thiếu : */
        "{a:1}",             /* key không có ngoặc kép */
        "{\"a\":\"x}",       /* chuỗi không đóng */
        "{\"a\":\"x\ny\"}",  /* xuống dòng thô trong chuỗi */
        "{\"a\":\"\\q\"}",   /* escape lạ */
        "{\"a\":\"\\u12\"}", /* \u thiếu chữ số */
        "{\"a\":1} rác",     /* rác sau } */
        "{\"a\":tru}",
        "{\"a\":-}",
        "{\"a\":01x}",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        if (fsp_json_validate(bad[i]) != -1) {
            fprintf(stderr, "[FAIL] accepted malformed JSON #%zu: %s\n", i, bad[i]);
            return 1;
        }
    }
    char out[16];
    CHECK(fsp_json_get_str("{\"a\":\"\\u0000x\"}", "a", out, sizeof(out)) == -1,
          "\\u0000 rejected (would truncate C string)");
    CHECK(fsp_json_get_str("{\"a\":\"\\udc00\"}", "a", out, sizeof(out)) == -1,
          "lone low surrogate rejected");
    CHECK(fsp_json_get_str(NULL, "a", out, sizeof(out)) == -1, "NULL json");
    CHECK(fsp_json_validate("{}") == 0 && fsp_json_has("{}", "a") == 0, "empty object ok");
    CHECK(fsp_json_validate("  { \"a\" : 1 }  ") == 0, "surrounding spaces ok");
    return 0;
}

static int test_first_line_only(void) {
    /* Phản hồi danh sách (SDD §3.3): JSON ở dòng đầu, các dòng TSV phía sau bị bỏ qua */
    const char *resp = "{\"status\":200,\"count\":2}\nBaoCao\td\t0\nnote.txt\tf\t12";
    long long v = 0;
    CHECK(fsp_json_validate(resp) == 0, "object + TSV lines is valid");
    CHECK(fsp_json_get_int(resp, "count", &v) == 0 && v == 2, "read first-line field");
    CHECK(fsp_json_has(resp, "BaoCao") == 0, "TSV content is not parsed as JSON");
    return 0;
}

static int test_escape_and_writer(void) {
    char out[64];
    size_t need = fsp_json_escape("a\"b\\c\nd\x01", out, sizeof(out));
    CHECK(strcmp(out, "a\\\"b\\\\c\\nd\\u0001") == 0, "escape output");
    CHECK(need == strlen(out), "returns full length");

    /* Bộ đệm nhỏ: không bao giờ cắt giữa một chuỗi escape */
    char tiny[4];
    need = fsp_json_escape("ab\"c", tiny, sizeof(tiny));
    CHECK(need == 5 && strcmp(tiny, "ab") == 0, "truncation keeps escapes whole");

    char buf[128];
    fsp_json_fields_t f;
    fsp_jf_init(&f, buf, sizeof(buf));
    fsp_jf_str(&f, "token", "x\"y");
    fsp_jf_int(&f, "size", -7);
    fsp_jf_bool(&f, "recursive", true);
    CHECK(!f.overflow, "fits");
    CHECK(strcmp(buf, "\"token\":\"x\\\"y\",\"size\":-7,\"recursive\":true") == 0, "fields");

    /* Gói lại thành object rồi đọc ngược: round-trip */
    char obj[160];
    snprintf(obj, sizeof(obj), "{%s}", buf);
    char tok[16];
    long long v = 0;
    bool b = false;
    CHECK(fsp_json_get_str(obj, "token", tok, sizeof(tok)) == 0 && strcmp(tok, "x\"y") == 0,
          "string round-trip");
    CHECK(fsp_json_get_int(obj, "size", &v) == 0 && v == -7, "int round-trip");
    CHECK(fsp_json_get_bool(obj, "recursive", &b) == 0 && b, "bool round-trip");

    char small[16];
    fsp_jf_init(&f, small, sizeof(small));
    fsp_jf_str(&f, "path", "/a/very/long/path");
    CHECK(f.overflow, "overflow detected");
    CHECK(strlen(small) < sizeof(small), "never writes past cap");
    return 0;
}

int main(void) {
    int passed = 0;
    printf("=== FLAT JSON HELPERS (C11) ===\n");
    RUN_TEST(test_get_string, passed);
    RUN_TEST(test_key_matching_is_structural, passed);
    RUN_TEST(test_int_and_bool, passed);
    RUN_TEST(test_malformed_rejected, passed);
    RUN_TEST(test_first_line_only, passed);
    RUN_TEST(test_escape_and_writer, passed);
    printf("ALL JSON TESTS PASSED (%d/%d)\n", passed, passed);
    return 0;
}
