#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Type Type;
typedef struct Node Node;

//
// strings.c
//

char *format(char *fmt, ...);

//
// tokenize.c
//

// トークンの種類
typedef enum {
    TK_IDENT,    // 識別子
    TK_PUNCT,    // 記号
    TK_KEYWORD,  // キーワード
    TK_STR,      // 文字列リテラル
    TK_NUM,      // 整数トークン
    TK_EOF,      // 入力の終わりを表すトークン
} TokenKind;

typedef struct Token Token;

// トークン型
struct Token {
    TokenKind kind; // トークンの型
    Token *next;    // 次の入力トークン
    int val;        // kindがTK_NUMの場合、その数値
    char *loc;      // トークンの位置
    int len;        // トークンの長さ
    Type *ty;       // kindがTK_STRの場合に用いる
    char *str;      // 文字列リテラルの内容（終端はヌル文字 '\0'）
};

void error(char *fmt, ...);
void error_at(char *loc, char *fmt, ...);
void error_tok(Token *tok, char *fmt, ...);
bool equal(Token *tok, char *op);
Token *skip(Token *tok, char *op);
bool consume(Token **rest, Token *tok, char *str);
Token *tokenize_file(char *filename);

//
// parse.c
//

// ローカル変数
typedef struct Obj Obj;
struct Obj {
    Obj *next;
    char *name;     // 変数名
    Type *ty;       // 型
    bool is_local;  // ローカルまたはグローバル変数

    // ローカル変数
    int offset;     // RBPレジスタからのオフセット

    // グローバル変数または関数
    bool is_function;

    // グローバル変数
    char *init_data;

    // 関数
    Obj *params;
    Node *body;
    Obj *locals;
    int stack_size;
};

typedef enum {
    ND_ADD,       // +
    ND_SUB,       // -
    ND_MUL,       // *
    ND_DIV,       // /
    ND_NEG,       // 単項演算子の -
    ND_EQ,        // ==
    ND_NE,        // !=
    ND_LT,        // <
    ND_LE,        // <=
    ND_ASSIGN,    // =
    ND_ADDR,      // 単項演算子の &
    ND_DEREF,     // 単項演算子の *
    ND_RETURN,    // "return"
    ND_IF,        // "if"
    ND_FOR,       // "for" または "while"
    ND_BLOCK,     // { ... }
    ND_FUNCALL,   // 関数呼び出し
    ND_EXPR_STMT, // 式文
    ND_STMT_EXPR, // 文式
    ND_VAR,       // 変数
    ND_NUM,       // 整数
} NodeKind;

// 抽象構文木のノードの型
struct Node {
    NodeKind kind; // ノードの型
    Node *next;    // 次のstmtのノード
    Type *ty;      // 型。例えば整数型や、整数型へのポインタ型
    Token *tok;    // 表示用のトークン

    Node *lhs;     // 左辺
    Node *rhs;     // 右辺

    // "if" または "for" 文
    Node *cond;
    Node *then;
    Node *els;
    Node *init;
    Node *inc;

    // ブロックまたは文式
    Node *body;

    // 関数呼び出し
    char *funcname;
    Node *args;

    Obj *var;      // kindがND_VARの場合のみ使う
    int val;       // kindがND_NUMの場合のみ使う
};

Obj *parse(Token *tok);

//
// type.c
//

typedef enum {
    TY_CHAR,
    TY_INT,
    TY_PTR,
    TY_FUNC,
    TY_ARRAY
} TypeKind;

struct Type {
    TypeKind kind;

    int size;    // sizeof() value

    // pointer-to 型、または array-of 型。C においてはポインタと配列は意味的に
    // 重複しているため、意図的に同じメンバ変数を用いている。

    // ポインタが期待されているような文脈の多くで、ある型がポインタなのかそうで
    // ないのかを判別するために、このメンバを "kind" メンバの代わりに調べる。
    // これは、多くの文脈における "array of T" を、C の仕様で要求されている
    // ように、あたかも "pointer to T" であるかのように自然に扱う、ということを
    // 意味する。
    Type *base;

    // 宣言
    Token *name;

    // 配列
    int array_len;

    // 関数の型
    Type *return_ty;
    Type *params;
    Type *next;
};

extern Type *ty_char;
extern Type *ty_int;

bool is_integer(Type *ty);
Type *copy_type(Type *ty);
Type *pointer_to(Type *base);
Type *func_type(Type* return_ty);
Type *array_of(Type* base, int len);
void add_type(Node *node);

//
// codegen.c
//

void codegen(Obj *prog, FILE *out);