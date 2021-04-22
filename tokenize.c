#include "chibicc.h"

// 入力されたファイル名
static char *current_filename;

// 入力プログラム
static char *current_input;

void error(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    exit(1);
}

// 以下の書式でエラーメッセージを報告しexitする
//
// foo.c:10: x = y + 1;
//               ^ <error message here>
void verror_at(int line_no, char *loc, char *fmt, va_list ap) {
    // `loc` を含む行を探す
    char *line = loc;
    while (current_input < line && line[-1] != '\n')
        line--;

    char *end = loc;
    while (*end != '\n')
        end++;

    // 該当の行を出力
    int indent = fprintf(stderr, "%s:%d: ", current_filename, line_no);
    fprintf(stderr, "%.*s\n", (int)(end - line), line);

    // エラーメッセージを表示
    int pos = loc - line + indent;

    fprintf(stderr, "%*s", pos, "");  // pos個の空白を出力
    fprintf(stderr, "^ ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    exit(1);
}

// tokenize に関するエラーを報告するための関数
void error_at(char *loc, char *fmt, ...) {
    // 行番号を取得
    int line_no = 1;
    for (char *p = current_input; p < loc; p++)
        if (*p == '\n')
            line_no++;

    va_list ap;
    va_start(ap, fmt);
    verror_at(line_no, loc, fmt, ap);
}

// parse に関するエラーを報告するための関数
void error_tok(Token *tok, char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    verror_at(tok->line_no, tok->loc, fmt, ap);
}

// トークンが指定した演算子であるかどうかを返す
bool equal(Token *tok, char *op) {
    return memcmp(tok->loc, op, tok->len) == 0 && op[tok->len] == '\0';
}

// 次のトークンが期待している記号のときには、トークンを１つ読み進める。
// それ以外の場合にはエラーを報告する。
Token *skip(Token *tok, char *op) {
    if (!equal(tok, op))
        error_tok(tok, "記号 '%s' が必要です", op);
    return tok->next;
}

// 指定された文字列を消費できたかどうかを返す
bool consume(Token **rest, Token *tok, char *str) {
    if (equal(tok, str)) {
        *rest = tok->next;
        return true;
    }
    *rest = tok;
    return false;
}

// 新しいトークンを作成してcurに繋げる
static Token *new_token(TokenKind kind, char *start, char *end) {
    Token *tok = calloc(1, sizeof(Token));
    tok->kind = kind;
    tok->loc = start;
    tok->len = end - start;
    return tok;
}

// 入力文字列の先頭が指定した文字列から始まっているかどうかを返す
static bool startswith(char *p, char *q) {
    return memcmp(p, q, strlen(q)) == 0;
}

// cが識別子の最初の文字として適当ならtrueを返す
static bool is_ident1(char c) {
    return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || c == '_';
}

// cが識別子の２文字目以降の文字として適当ならtrueを返す
static bool is_ident2(char c) {
    return is_ident1(c) || ('0' <= c && c <= '9');
}

static int from_hex(char c) {
    if ('0' <= c && c <= '9')
        return c - '0';
    if ('a' <= c && c <= 'f')
        return c - 'a' + 10;
    return c - 'A' + 10;
}

static int read_punct(char *p) {
    if (startswith(p, "==") || startswith(p, "!=") ||
        startswith(p, "<=") || startswith(p, ">="))
        return 2;

    return ispunct(*p) ? 1 : 0;
}

static bool is_keyword(Token *tok) {
    static char *kw[] = {
        "return", "if", "else", "for", "while", "int", "sizeof", "char"
    };

    for (int i = 0; i < sizeof(kw) / sizeof(*kw); i++)
        if (equal(tok, kw[i]))
            return true;
    return false;
}

static int read_escaped_char(char **new_pos, char *p) {
    if ('0' <= *p && *p <= '7') {
        // ８進数を読む
        int c = *p++ - '0';
        if ('0' <= *p && *p <= '7') {
            c = (c << 3) + (*p++ - '0');
            if ('0' <= *p && *p <= '7')
                c = (c << 3) + (*p++ - '0');
        }
        *new_pos = p;
        return c;
    }

    if (*p == 'x') {
        // １６進数を読む
        p++;
        if (!isxdigit(*p))
            error_at(p, "正しくない１６進数エスケープシーケンスです");

        int c = 0;
        for (; isxdigit(*p); p++)
            c = (c << 4) + from_hex(*p);
        *new_pos = p;
        return c;
    }

    *new_pos = p + 1;

    // エスケープシーケンスはそれら自身を用いて定義する。例えば、'\n'は'\n'を
    // 用いて定義する。このトートロジー的な定義は、我々のコンパイラをコンパイル
    // するコンパイラが'\n'とは実際には何かを知っているからこそ機能する。つまり
    // 我々は、我々のコンパイラをコンパイルするコンパイラから、ASCIIコード'\n'
    // を「受け継いでいる」のである。
    //
    // この事実はコンパイラの正しさのみならず、出力されるコードのセキュリティ
    // にまで大きな影響を与える。これについてもっと知りたい場合は、ケン・トンプ
    // ソンによる "Reflections on Trusting Trust"（「信用を信用することができる
    // だろうか」）を読まれたし。
    // https://github.com/rui314/chibicc/wiki/thompson1984.pdf
    switch (*p) {
    case 'a': return '\a';
    case 'b': return '\b';
    case 't': return '\t';
    case 'n': return '\n';
    case 'v': return '\v';
    case 'f': return '\f';
    case 'r': return '\r';
    // [GNU] ASCIIの Esc 文字のために \e があるのは GNU C extension の一つである
    case 'e': return 27;
    default: return *p;
    }
}

// 閉じる側のダブルクォートを探す
static char *string_literal_end(char *p) {
    char *start = p;
    for (; *p != '"'; p++) {
        if (*p == '\n' || *p == '\0')
            error_at(start, "文字列リテラルが閉じられていません");
        if (*p == '\\')
            p++;
    }
    return p;
}

static Token *read_string_literal(char *start) {
    char *end = string_literal_end(start + 1);
    char *buf = calloc(1, end - start);
    int len = 0;

    for (char *p = start + 1; p < end;) {
        if (*p == '\\')
            buf[len++] = read_escaped_char(&p, p + 1);
        else
            buf[len++] = *p++;
    }

    Token *tok = new_token(TK_STR, start, end + 1);
    tok->ty = array_of(ty_char, len + 1);
    tok->str = buf;
    return tok;
}

static void convert_keywords(Token *tok) {
    for (Token *t = tok; t->kind != TK_EOF; t = t->next)
        if (is_keyword(t))
            t->kind = TK_KEYWORD;
}

// 全てのトークンについて行の情報を初期化
static void add_line_numbers(Token *tok) {
    char *p = current_input;
    int n = 1;

    do {
        if (p == tok->loc) {
            tok->line_no = n;
            tok = tok->next;
        }
        if (*p == '\n')
            n++;
    } while (*p++);
}

// 入力文字列pをトークナイズしてそれを返す
static Token *tokenize(char *filename, char *p) {
    current_filename = filename;
    current_input = p;
    Token head = {};
    Token *cur = &head;

    while (*p) {
        // 行コメントをスキップ
        if (startswith(p, "//")) {
            p += 2;
            while (*p != '\n')
                p++;
            continue;
        }

        // ブロックコメントをスキップ
        if (startswith(p, "/*")) {
            char *q = strstr(p + 2, "*/");
            if (!q)
                error_at(p, "ブロックコメントが閉じられていません");
            p = q + 2;
            continue;
        }

        // 空白文字をスキップ
        if (isspace(*p)) {
            p++;
            continue;
        }

        // 数値リテラル
        if (isdigit(*p)) {
            cur = cur->next = new_token(TK_NUM, p, p);
            char *q = p;
            cur->val = strtoul(p, &p, 10);
            cur->len = p - q;
            continue;
        }

        // 文字列リテラル
        if (*p == '"') {
            cur = cur->next = read_string_literal(p);
            p += cur->len;
            continue;
        }

        // 識別子あるいはキーワード
        if (is_ident1(*p)) {
            char *start = p;
            do {
                p++;
            } while (is_ident2(*p));
            cur = cur->next = new_token(TK_IDENT, start, p);
            continue;
        }

        // 記号
        int punct_len = read_punct(p);
        if (punct_len) {
            cur = cur->next = new_token(TK_PUNCT, p, p + punct_len);
            p += cur->len;
            continue;
        }

        error_at(p, "トークナイズできません");
    }

    cur = cur->next = new_token(TK_EOF, p, p);
    add_line_numbers(head.next);
    convert_keywords(head.next);
    return head.next;
}

// 与えられたファイルの内容を返す
static char *read_file(char *path) {
    FILE *fp;

    if (strcmp(path, "-") == 0) {
        // 慣習として、与えられたファイル名が "-" のときは stdin から読む
        fp = stdin;
    } else {
        fp = fopen(path, "r");
        if (!fp)
            error("cannot open %s: %s", path, strerror(errno));
    }

    char *buf;
    size_t buflen;
    FILE *out = open_memstream(&buf, &buflen);

    // ファイル全体を読む
    for (;;) {
        char buf2[4096];
        int n = fread(buf2, 1, sizeof(buf2), fp);
        if (n == 0)
            break;
        fwrite(buf2, 1, n, out);
    }

    if (fp != stdin)
        fclose(fp);

    // 最後の行が正しく '\n' で終わっていることを確認する
    fflush(out);
    if (buflen == 0 || buf[buflen - 1] != '\n')
        fputc('\n', out);
    fputc('\0', out);
    fclose(out);
    return buf;
}

Token *tokenize_file(char *path) {
    return tokenize(path, read_file(path));
}