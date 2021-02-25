#include "9cc.h"

// 入力プログラム
static char *user_input;

void verror_at(char *loc, char *fmt, va_list ap) {
    int pos = loc - user_input;
    fprintf(stderr, "%s\n", user_input);
    fprintf(stderr, "%*s", pos, "");  // pos個の空白を出力
    fprintf(stderr, "^ ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    exit(1);
}

// tokenize に関するエラーを報告するための関数
void error_at(char *loc, char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    verror_at(loc, fmt, ap);
}

// parse に関するエラーを報告するための関数
void error_tok(Token *tok, char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    verror_at(tok->str, fmt, ap);
}

// トークンが指定した演算子であるかどうかを返す
bool equal(Token *tok, char *op) {
    if (memcmp(tok->str, op, tok->len) == 0 && op[tok->len] == '\0') {
        tok = tok->next;
        return true;
    }
    return false;
}

// 次のトークンが期待している記号のときには、トークンを１つ読み進める。
// それ以外の場合にはエラーを報告する。
void expect(Token **rest, Token *tok, char *op) {
    if (tok->kind != TK_RESERVED ||
        strlen(op) != tok->len ||
        memcmp(tok->str, op, tok->len))
        error_at(tok->str, "\"%s\"ではありません", op);
    *rest = tok->next;
}

// 次のトークンが数値の場合、トークンを１つ読み進めてその数値を返す。
// それ以外の場合にはエラーを報告する。
int expect_number(Token **rest, Token *tok) {
    if (tok->kind != TK_NUM)
        error_at(tok->str, "数ではありません");
    int val = tok->val;
    *rest = tok->next;
    return val;
}

// 新しいトークンを作成してcurに繋げる
Token *new_token(TokenKind kind, char *start, char *end) {
    Token *tok = calloc(1, sizeof(Token));
    tok->kind = kind;
    tok->str = start;
    tok->len = end - start;
    return tok;
}

// 入力文字列の先頭が指定した文字列から始まっているかどうかを返す
bool startswith(char *p, char *q) {
    return memcmp(p, q, strlen(q)) == 0;
}

int read_punct(char *p) {
    if (startswith(p, "==") || startswith(p, "!=") ||
        startswith(p, "<=") || startswith(p, ">="))
        return 2;
    
    return ispunct(*p) ? 1 : 0;
}

// 入力文字列pをトークナイズしてそれを返す
Token *tokenize(char *p) {
    user_input = p;
    Token head = {};
    head.next = NULL;
    Token *cur = &head;

    while (*p) {
        // 空白文字をスキップ
        if (isspace(*p)) {
            p++;
            continue;
        }

        if (isdigit(*p)) {
            cur = cur->next = new_token(TK_NUM, p, p);
            char *q = p;
            cur->val = strtol(p, &p, 10);
            cur->len = p - q;
            continue;
        }

        int punct_len = read_punct(p);
        if (punct_len) {
            cur = cur->next = new_token(TK_RESERVED, p, p + punct_len);
            p += cur->len;
            continue;
        }

        error_at(p, "トークナイズできません");
    }

    cur = cur->next = new_token(TK_EOF, p, p);
    return head.next;
}