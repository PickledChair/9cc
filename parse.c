#include "chibicc.h"

// ローカル・グローバル変数または typedef のためのスコープ
typedef struct VarScope VarScope;
struct VarScope {
    VarScope *next;
    char *name;
    Obj *var;
    Type *type_def;
};

// 構造体タグのためのスコープ
typedef struct TagScope TagScope;
struct TagScope {
    TagScope *next;
    char *name;
    Type *ty;
};

// ブロックスコープの表現
typedef struct Scope Scope;
struct Scope {
    Scope *next;

    // C は２つのブロックスコープを持っている。一つは変数のための。
    // もう一つは構造体タグのためのものである。
    VarScope *vars;
    TagScope *tags;
};

// typedef や extern といった変数の属性
typedef struct {
    bool is_typedef;
} VarAttr;

// パースしている間に作成されたすべてのローカル変数インスタンスは
// このスタックに積み重ねられていく
static Obj *locals;

// ローカル変数と同様、グローバル変数もこのスタックに積み重ねる
static Obj *globals;

static Scope *scope = &(Scope){};

// スコープに入る
static void enter_scope(void) {
    Scope *sc = calloc(1, sizeof(Scope));
    sc->next = scope;
    scope = sc;
}

// スコープを抜ける
static void leave_scope(void) {
    scope = scope->next;
}

// ローカル変数を名前によって探す
static VarScope *find_var(Token *tok) {
    for (Scope *sc = scope; sc; sc = sc->next)
        for (VarScope *sc2 = sc->vars; sc2; sc2 = sc2->next)
            if (equal(tok, sc2->name))
                return sc2;
    return NULL;
}

// 構造体タグを名前によって探す
static Type *find_tag(Token *tok) {
    for (Scope *sc = scope; sc; sc = sc->next)
        for (TagScope *sc2 = sc->tags; sc2; sc2 = sc2->next)
            if (equal(tok, sc2->name))
                return sc2->ty;
    return NULL;
}

// メモリの確保と、指定された種類のノードの作成
static Node *new_node(NodeKind kind, Token *tok) {
    Node *node = calloc(1, sizeof(Node));
    node->kind = kind;
    node->tok = tok;
    return node;
}

// 新しい二項演算子のノードを作成する
static Node *new_binary(NodeKind kind, Node *lhs, Node *rhs, Token *tok) {
    Node *node = new_node(kind, tok);
    node->lhs = lhs;
    node->rhs = rhs;
    return node;
}

// 新しい単項演算子のノードを作成する
static Node *new_unary(NodeKind kind, Node *expr, Token *tok) {
    Node *node = new_node(kind, tok);
    node->lhs = expr;
    return node;
}

// 新しい数値のノードを作成する
static Node *new_num(int64_t val, Token *tok) {
    Node *node = new_node(ND_NUM, tok);
    node->val = val;
    return node;
}

// 新しい変数のノードを作成する
static Node *new_var_node(Obj *var, Token *tok) {
    Node *node = new_node(ND_VAR, tok);
    node->var = var;
    return node;
}

// 現在のスコープの変数スタックに新しい変数をプッシュする
static VarScope *push_scope(char *name) {
    VarScope *sc = calloc(1, sizeof(VarScope));
    sc->name = name;
    sc->next = scope->vars;
    scope->vars = sc;
    return sc;
}

// 新しい変数を作成する
static Obj *new_var(char *name, Type *ty) {
    Obj *var = calloc(1, sizeof(Obj));
    var->name = name;
    var->ty = ty;
    push_scope(name)->var = var;
    return var;
}

// 新しいローカル変数を作成する
static Obj *new_lvar(char *name, Type *ty) {
    Obj *var = new_var(name, ty);
    var->is_local = true;
    var->name = name;
    var->ty = ty;
    var->next = locals;
    locals = var;
    return var;
}

// 新しいグローバル変数を作成する
static Obj *new_gvar(char *name, Type *ty) {
    Obj *var = new_var(name, ty);
    var->next = globals;
    globals = var;
    return var;
}

static char *new_unique_name(void) {
    static int id = 0;
    return format(".L..%d", id++);
}

static Obj *new_anon_gvar(Type *ty) {
    return new_gvar(new_unique_name(), ty);
}

static Obj *new_string_literal(char *p, Type *ty) {
    Obj *var = new_anon_gvar(ty);
    var->init_data = p;
    return var;
}

// 識別子のトークンから識別子の文字列を得る
static char *get_ident(Token *tok) {
    if (tok->kind != TK_IDENT)
        error_tok(tok, "トークンの種類が識別子である必要があります");
    return strndup(tok->loc, tok->len);
}

static Type *find_typedef(Token *tok) {
    if (tok->kind == TK_IDENT) {
        VarScope *sc = find_var(tok);
        if (sc)
            return sc->type_def;
    }
    return NULL;
}

// 数値のトークンから数値を得る
static int get_number(Token *tok) {
    if (tok->kind != TK_NUM)
        error_tok(tok, "トークンの種類が数値である必要があります");
    return tok->val;
}

static Node *stmt(Token **rest, Token *tok);
static Node *compound_stmt(Token **rest, Token *tok);
static Type *declspec(Token **rest, Token *tok, VarAttr *attr);
static Type *declarator(Token **rest, Token *tok, Type* ty);
static Node *declaration(Token **rest, Token *tok, Type *basety);
static Node *expr_stmt(Token **rest, Token *tok);
static Node *assign(Token **rest, Token *tok);
static Node *expr(Token **rest, Token *tok);
static Node *equality(Token **rest, Token *tok);
static Node *relational(Token **rest, Token *tok);
static Node *add(Token **rest, Token *tok);
static Node *mul(Token **rest, Token *tok);
static Type *struct_decl(Token **rest, Token *tok);
static Type *union_decl(Token **rest, Token *tok);
static Node *postfix(Token **rest, Token *tok);
static Node *unary(Token **rest, Token *tok);
static Node *primary(Token **rest, Token *tok);
static Token *parse_typedef(Token *tok, Type *basety);

// 与えられたトークンが型を表している場合、trueを返す
static bool is_typename(Token *tok) {
    static char *kw[] = {
        "void", "char", "short", "int", "long", "struct", "union",
        "typedef"
    };

    for (int i = 0; i < sizeof(kw) / sizeof(*kw); i++)
        if (equal(tok, kw[i]))
            return true;
    return find_typedef(tok);
}

// stmtをパースする
// stmt = "return" expr ";"
//      | "if" "(" expr ")" stmt ("else" stmt)?
//      | "for" "(" expr-stmt expr? ";" expr? ")" stmt
//      | "while" "(" expr ")" stmt
//      | "{" compund-stmt
//      | expr-stmt
static Node *stmt(Token **rest, Token *tok) {
    if (equal(tok, "return")) {
        Node *node = new_node(ND_RETURN, tok);
        node->lhs = expr(&tok, tok->next);
        *rest = skip(tok, ";");
        return node;
    }

    if (equal(tok, "if")) {
        Node *node = new_node(ND_IF, tok);
        tok = skip(tok->next, "(");
        node->cond = expr(&tok, tok);
        tok = skip(tok, ")");
        node->then = stmt(&tok, tok);
        if (equal(tok, "else"))
            node->els = stmt(&tok, tok->next);
        *rest = tok;
        return node;
    }

    if (equal(tok, "for")) {
        Node *node = new_node(ND_FOR, tok);
        tok = skip(tok->next, "(");

        node->init = expr_stmt(&tok, tok);

        if (!equal(tok, ";"))
            node->cond = expr(&tok, tok);
        tok = skip(tok, ";");

        if (!equal(tok, ")"))
            node->inc = expr(&tok, tok);
        tok = skip(tok, ")");

        node->then = stmt(rest, tok);
        return node;
    }

    if (equal(tok, "while")) {
        Node *node = new_node(ND_FOR, tok);
        tok = skip(tok->next, "(");
        node->cond = expr(&tok, tok);
        tok = skip(tok, ")");
        node->then = stmt(rest, tok);
        return node;
    }

    if (equal(tok, "{"))
        return compound_stmt(rest, tok->next);

    return expr_stmt(rest, tok);
}

// compound-stmtをパースする
// compound-stmt = (typedef | declaration | stmt)* "}"
static Node *compound_stmt(Token **rest, Token *tok) {
    Node *node = new_node(ND_BLOCK, tok);

    Node head = {};
    Node *cur = &head;

    enter_scope();

    while (!equal(tok, "}")) {
        if (is_typename(tok)) {
            VarAttr attr = {};
            Type *basety = declspec(&tok, tok, &attr);

            if (attr.is_typedef) {
                tok = parse_typedef(tok, basety);
                continue;
            }

            cur = cur->next = declaration(&tok, tok, basety);
        } else {
            cur = cur->next = stmt(&tok, tok);
        }
        add_type(cur);
    }

    leave_scope();

    node->body = head.next;
    *rest = tok->next;
    return node;
}

// 新しい TagScope を現在のスコープにプッシュする
static void push_tag_scope(Token *tok, Type *ty) {
    TagScope *sc = calloc(1, sizeof(TagScope));
    sc->name = strndup(tok->loc, tok->len);
    sc->ty = ty;
    sc->next = scope->tags;
    scope->tags = sc;
}

// declspecをパースする
// declspec = ("void" | "char" | "short" | "int" | "long"
//              | "typedef"
//              | struct-decl | union-decl | typedef-name)+
//
// 型指定子の中の型名の順番は重要ではない。例えば、`int long static` は
// `static long int` と同じ意味である。これは、`long`や`short`が指定されて
// いれば、`int`を省略することができるので、`static long`と書くこともできる。
// しかし、`char int`のようなものは有効な型指定子ではない。限られた型名の組み
// 合わせのみを受け入れる必要がある。
//
// この関数では，それまでの型名が表す「現在の」型オブジェクトを維持したまま，
// 各型名の出現回数を数える。型名ではないトークンに到達すると，現在の型オブジェクト
// を返す。
static Type *declspec(Token **rest, Token *tok, VarAttr *attr) {
    // すべての型名のカウンターとして1つの整数を使用する。例えば、ビット0と
    // ビット1は、これまでに "void" というキーワードを何回見たかを表している。
    // これを利用して、以下のようにswitch文を使うことができる。
    enum {
        VOID  = 1 << 0,
        CHAR  = 1 << 2,
        SHORT = 1 << 4,
        INT   = 1 << 6,
        LONG  = 1 << 8,
        OTHER = 1 << 10,
    };

    Type *ty = ty_int;
    int counter = 0;

    while (is_typename(tok)) {
        // "typedef" キーワードを扱う
        if (equal(tok, "typedef")) {
            if (!attr)
                error_tok(tok, "このコンテキストではストレージクラス指定子は許可されていません");
            attr->is_typedef = true;
            tok = tok->next;
            continue;
        }

        // ユーザー定義型を扱う
        Type *ty2 = find_typedef(tok);
        if (equal(tok, "struct") || equal(tok, "union") || ty2) {
            if (counter)
                break;

            if (equal(tok, "struct")) {
                ty = struct_decl(&tok, tok->next);
            } else if (equal(tok, "union")) {
                ty = union_decl(&tok, tok->next);
            } else {
                ty = ty2;
                tok = tok->next;
            }

            counter += OTHER;
            continue;
        }

        // 組み込み型を扱う
        if (equal(tok, "void"))
            counter += VOID;
        else if (equal(tok, "char"))
            counter += CHAR;
        else if (equal(tok, "short"))
            counter += SHORT;
        else if (equal(tok, "int"))
            counter += INT;
        else if (equal(tok, "long"))
            counter += LONG;
        else
            unreachable();

        switch (counter) {
        case VOID:
            ty = ty_void;
            break;
        case CHAR:
            ty = ty_char;
            break;
        case SHORT:
        case SHORT + INT:
            ty = ty_short;
            break;
        case INT:
            ty = ty_int;
            break;
        case LONG:
        case LONG + INT:
        case LONG + LONG:
        case LONG + LONG + INT:
            ty = ty_long;
            break;
        default:
            error_tok(tok, "不正な型です");
        }

        tok = tok->next;
    }

    *rest = tok;
    return ty;
}

// func-paramsをパースする
// func-params = (param ("," param)*)? ")"
// param       = declspec declarator
static Type *func_params(Token **rest, Token *tok, Type *ty) {
    Type head = {};
    Type *cur = &head;

    while (!equal(tok, ")")) {
        if (cur != &head)
            tok = skip(tok, ",");
        Type *basety = declspec(&tok, tok, NULL);
        Type *ty = declarator(&tok, tok, basety);
        cur = cur->next = copy_type(ty);
    }

    ty = func_type(ty);
    ty->params = head.next;
    *rest = tok->next;
    return ty;
}

// type-suffixをパースする
// type-suffix = ("(" func-params
//             | "[" num "]" type-suffix
//             | ε
static Type *type_suffix(Token **rest, Token *tok, Type *ty) {
    if (equal(tok, "("))
        return func_params(rest, tok->next, ty);

    if (equal(tok, "[")) {
        int sz = get_number(tok->next);
        tok = skip(tok->next->next, "]");
        ty = type_suffix(rest, tok, ty);
        return array_of(ty, sz);
    }

    *rest = tok;
    return ty;
}

// declaratorをパースする
// declarator = "*"* ("(" ident ")" | "(" declarator ")" | ident ) type-suffix
static Type *declarator(Token **rest, Token *tok, Type *ty) {
    while (consume(&tok, tok, "*"))
        ty = pointer_to(ty);

    if (equal(tok, "(")) {
        Token *start = tok;
        Type dummy = {};
        declarator(&tok, start->next, &dummy);
        tok = skip(tok, ")");
        ty = type_suffix(rest, tok, ty);
        return declarator(&tok, start->next, ty);
    }

    if (tok->kind != TK_IDENT)
        error_tok(tok, "変数名がありません");

    ty = type_suffix(rest, tok->next, ty);
    ty->name = tok;
    return ty;
}

// abstract-declaratorをパースする
// abstract-declarator = "*"* ("(" abstract-declarator ")")? type-suffix
static Type *abstract_declarator(Token **rest, Token *tok, Type *ty) {
    while (equal(tok, "*")) {
        ty = pointer_to(ty);
        tok = tok->next;
    }

    if (equal(tok, "(")) {
        Token *start = tok;
        Type dummy = {};
        abstract_declarator(&tok, start->next, &dummy);
        tok = skip(tok, ")");
        ty = type_suffix(rest, tok, ty);
        return abstract_declarator(&tok, start->next, ty);
    }

    return type_suffix(rest, tok, ty);
}

// type-nameをパースする
// type-name = declspec abstract-declarator
static Type *typename(Token **rest, Token *tok) {
    Type *ty = declspec(&tok, tok, NULL);
    return abstract_declarator(rest, tok, ty);
}

// declarationをパースする
// declaration = declspec (declarator ("=" expr)? ("," declarator ("=" expr)?)*)? ";"
static Node *declaration(Token **rest, Token *tok, Type *basety) {
    Node head = {};
    Node *cur = &head;
    int i = 0;

    while(!equal(tok, ";")) {
        if (i++ > 0)
            tok = skip(tok, ",");

        Type *ty = declarator(&tok, tok, basety);
        if (ty->kind == TY_VOID)
            error_tok(tok, "void 型の変数を宣言しています");

        Obj *var = new_lvar(get_ident(ty->name), ty);

        if (!equal(tok, "="))
            continue;

        Node *lhs = new_var_node(var, ty->name);
        Node *rhs = assign(&tok, tok->next);
        Node *node = new_binary(ND_ASSIGN, lhs, rhs, tok);
        cur = cur->next = new_unary(ND_EXPR_STMT, node, tok);
    }

    Node *node = new_node(ND_BLOCK, tok);
    node->body = head.next;
    *rest = tok->next;
    return node;
}

// expr-stmtをパースする
// expr-stmt = expr? ";"
static Node *expr_stmt(Token **rest, Token *tok) {
    if (equal(tok, ";")) {
        *rest = tok->next;
        return new_node(ND_BLOCK, tok);
    }

    Node *node = new_node(ND_EXPR_STMT, tok);
    node->lhs = expr(&tok, tok);
    *rest = skip(tok, ";");
    return node;
}

// exprをパースする
// expr = assign ("," expr)?
static Node *expr(Token **rest, Token *tok) {
    Node *node = assign(&tok, tok);

    if (equal(tok, ","))
        return new_binary(ND_COMMA, node, expr(rest, tok->next), tok);

    *rest = tok;
    return node;
}

// assignをパースする
// assign = equality ("=" assign)?
static Node *assign(Token **rest, Token *tok) {
    Node *node = equality(&tok, tok);

    if (equal(tok, "="))
        return new_binary(ND_ASSIGN, node, assign(rest, tok->next), tok);

    *rest = tok;
    return node;
}

// equalityをパースする
// equality = relational ("==" relational | "!=" relational)*
static Node *equality(Token **rest, Token *tok) {
    Node *node = relational(&tok, tok);

    for (;;) {
        Token *start = tok;

        if (equal(tok, "==")) {
            node = new_binary(ND_EQ, node, relational(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, "!=")) {
            node = new_binary(ND_NE, node, relational(&tok, tok->next), start);
            continue;
        }

        *rest = tok;
        return node;
    }
}

// relationalをパースする
// relational = add ("<" add | "<=" add | ">" add | ">=" add)*
static Node *relational(Token **rest, Token *tok) {
    Node *node = add(&tok, tok);

    for (;;) {
        Token *start = tok;

        if (equal(tok, "<")) {
            node = new_binary(ND_LT, node, add(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, "<=")) {
            node = new_binary(ND_LE, node, add(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, ">")) {
            node = new_binary(ND_LT, add(&tok, tok->next), node, start);
            continue;
        }

        if (equal(tok, ">=")) {
            node = new_binary(ND_LE, add(&tok, tok->next), node, start);
            continue;
        }

        *rest = tok;
        return node;
    }
}

// C言語では、+演算子はポインタ演算を実現するためにオーバーロードされている。
// pがポインタであるとき、p+nはpにnを足すという意味にはならない。
// pにsizeof(*p)*nを足す、という意味になる。
// したがってp+nが指し示すのはpよりも前方に「n要素」進んだ場所である。
// （「nバイト」進んだ場所ではない。）
// このため、ポインタ値へ整数値を足すときは、前もって整数値をスケールしておく
// 必要がある。この関数はこのスケーリングを担当する。
static Node *new_add(Node *lhs, Node *rhs, Token *tok) {
    add_type(lhs);
    add_type(rhs);

    // 数値 + 数値
    if (is_integer(lhs->ty) && is_integer(rhs->ty))
        return new_binary(ND_ADD, lhs, rhs, tok);

    // ポインタどうしの加算は禁止
    if (lhs->ty->base && rhs->ty->base)
        error_tok(tok, "正しくないオペランドです");

    // 「num + ptr」は「ptr + num」に正規化する
    if (!lhs->ty->base && rhs->ty->base) {
        Node *tmp = lhs;
        lhs = rhs;
        rhs = tmp;
    }

    // ptr + num
    rhs = new_binary(ND_MUL, rhs, new_num(lhs->ty->base->size, tok), tok);  // スケーリング
    return new_binary(ND_ADD, lhs, rhs, tok);
}

// +演算子のように、-演算子もポインタ型のためにオーバーロードする
static Node *new_sub(Node *lhs, Node *rhs, Token *tok) {
    add_type(lhs);
    add_type(rhs);

    // num - num
    if (is_integer(lhs->ty) && is_integer(rhs->ty))
        return new_binary(ND_SUB, lhs, rhs, tok);

    // ptr - num
    if (lhs->ty->base && is_integer(rhs->ty)) {
        rhs = new_binary(ND_MUL, rhs, new_num(lhs->ty->base->size, tok), tok);
        add_type(rhs);
        Node *node = new_binary(ND_SUB, lhs, rhs, tok);
        node->ty = lhs->ty;
        return node;
    }

    // ptr - ptr（２つの要素間に何個の要素があるかを返す）
    if (lhs->ty->base && rhs->ty->base) {
        Node *node = new_binary(ND_SUB, lhs, rhs, tok);
        node->ty = ty_int;
        return new_binary(ND_DIV, node, new_num(lhs->ty->base->size, tok), tok);
    }

    error_tok(tok, "正しくないオペランドです");
}

// addをパースする
// add = mul ("+" mul | "-" mul)*
static Node *add(Token **rest, Token *tok) {
    Node *node = mul(&tok, tok);

    for (;;) {
        Token *start = tok;

        if (equal(tok, "+")) {
            node = new_add(node, mul(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, "-")) {
            node = new_sub(node, mul(&tok, tok->next), start);
            continue;
        }

        *rest = tok;
        return node;
    }
}

// mulをパースする
// mul = unary ("*" unary | "/" unary)*
static Node *mul(Token **rest, Token *tok) {
    Node *node = unary(&tok, tok);

    for (;;) {
        Token *start = tok;

        if (equal(tok, "*")) {
            node = new_binary(ND_MUL, node, unary(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, "/")) {
            node = new_binary(ND_DIV, node, unary(&tok, tok->next), start);
            continue;
        }

        *rest = tok;
        return node;
    }
}

// unaryをパースする
// unary = ("+" | "-" | "*" | "&")? unary
//       | postfix
static Node *unary(Token **rest, Token *tok) {
    if (equal(tok, "+"))
        return unary(rest, tok->next);
    if (equal(tok, "-"))
        return new_unary(ND_NEG, unary(rest, tok->next), tok);
    if (equal(tok, "&"))
        return new_unary(ND_ADDR, unary(rest, tok->next), tok);
    if (equal(tok, "*"))
        return new_unary(ND_DEREF, unary(rest, tok->next), tok);
    return postfix(rest, tok);
}

// struct-membersをパースする
// struct-members = (declspec declarator ("," declarator)* ";")*
static void struct_members(Token **rest, Token *tok, Type *ty) {
    Member head = {};
    Member *cur = &head;

    while(!equal(tok, "}")) {
        Type *basety = declspec(&tok, tok, NULL);
        int i = 0;

        while (!consume(&tok, tok, ";")) {
            if (i++)
                tok = skip(tok, ",");

            Member *mem = calloc(1, sizeof(Member));
            mem->ty = declarator(&tok, tok, basety);
            mem->name = mem->ty->name;
            cur = cur->next = mem;
        }
    }

    *rest = tok->next;
    ty->members = head.next;
}

// struct-union-declをパースする
// struct-union-decl = ident? ("{" struct-members)?
static Type *struct_union_decl(Token **rest, Token *tok) {
    // 構造体タグを読む
    Token *tag = NULL;
    if (tok->kind == TK_IDENT) {
        tag = tok;
        tok = tok->next;
    }

    if (tag && !equal(tok, "{")) {
        Type *ty = find_tag(tag);
        if (!ty)
            error_tok(tag, "不明な構造体型です");
        *rest = tok;
        return ty;
    }

    // 構造体オブジェクトをコンストラクトする
    Type *ty = calloc(1, sizeof(Type));
    ty->kind = TY_STRUCT;
    struct_members(rest, tok->next, ty);
    ty->align = 1;

    // もし構造体名が与えられていたら、その構造体型を登録する
    if (tag)
        push_tag_scope(tag, ty);
    return ty;
}

// struct-decl をパースする
// struct-decl = struct-union-decl
static Type *struct_decl(Token **rest, Token *tok) {
    Type *ty = struct_union_decl(rest, tok);
    ty->kind = TY_STRUCT;

    // 構造体内でのオフセットを各メンバに代入する
    int offset = 0;
    for (Member *mem = ty->members; mem; mem = mem->next) {
        offset = align_to(offset, mem->ty->align);
        mem->offset = offset;
        offset += mem->ty->size;

        if (ty->align < mem->ty->align)
            ty->align = mem->ty->align;
    }
    ty->size = align_to(offset, ty->align);
    return ty;
}

// union-decl をパースする
// union-decl = struct-union-decl
static Type *union_decl(Token **rest, Token *tok) {
    Type *ty = struct_union_decl(rest, tok);
    ty->kind = TY_UNION;

    // もし共用体なら、オフセットを代入する必要はない。なぜなら、
    // それらはすでにゼロで初期化されているからである。それでも、
    // アラインメントとサイズを計算する必要はある。
    for (Member *mem = ty->members; mem; mem = mem->next) {
        if (ty->align < mem->ty->align)
            ty->align = mem->ty->align;
        if (ty->size < mem->ty->size)
            ty->size = mem->ty->size;
    }
    ty->size = align_to(ty->size, ty->align);
    return ty;
}

// 指定の構造体の型に指定のメンバがあればそれを返す。なければエラー
static Member *get_struct_member(Type *ty, Token *tok) {
    for (Member *mem = ty->members; mem; mem = mem->next)
        if (mem->name->len == tok->len &&
            !strncmp(mem->name->loc, tok->loc, tok->len))
            return mem;
    error_tok(tok, "指定のメンバがありません");
}

// 構造体メンバへのアクセスに必要な情報を集める
static Node *struct_ref(Node *lhs, Token *tok) {
    add_type(lhs);
    if (lhs->ty->kind != TY_STRUCT && lhs->ty->kind != TY_UNION)
        error_tok(lhs->tok, "構造体でも共用体でもありません");

    Node *node = new_unary(ND_MEMBER, lhs, tok);
    node->member = get_struct_member(lhs->ty, tok);
    return node;
}

// postfixをパースする
// postfix = primary ("[" expr "]" | "." ident | "->" ident)*
static Node *postfix(Token **rest, Token *tok) {
    Node *node = primary(&tok, tok);

    for (;;) {
        if (equal(tok, "[")) {
            // x[y] は *(x+y) と同じ意味
            Token *start = tok;
            Node *idx = expr(&tok, tok->next);
            tok = skip(tok, "]");
            node = new_unary(ND_DEREF, new_add(node, idx, start), start);
            continue;
        }

        if (equal(tok, ".")) {
            node = struct_ref(node, tok->next);
            tok = tok->next->next;
            continue;
        }

        if (equal(tok, "->")) {
            // x->y は (*x).y を縮めたもの
            node = new_unary(ND_DEREF, node, tok);
            node = struct_ref(node, tok->next);
            tok = tok->next->next;
            continue;
        }

        *rest = tok;
        return node;
    }
}

// funcallをパースする
// funcall = ident "(" (assign ("," assign)*)? ")"
static Node *funcall(Token **rest, Token *tok) {
    Token *start = tok;
    tok = tok->next->next;

    Node head = {};
    Node *cur = &head;

    while (!equal(tok, ")")) {
        if (cur != &head)
            tok = skip(tok, ",");
        cur = cur->next = assign(&tok, tok);
    }

    *rest = skip(tok, ")");

    Node *node = new_node(ND_FUNCALL, start);
    node->funcname = strndup(start->loc, start->len);
    node->args = head.next;
    return node;
}

// primaryをパースする
// primary = "(" "{" stmt+ "}" ")"
//         | "(" expr ")"
//         | "sizeof" "(" type-name ")"
//         | "sizeof" unary
//         | ident func-args?
//         | str
//         | num
static Node *primary(Token **rest, Token *tok) {
    Token *start = tok;

    // まずGNU拡張である式文が来る場合はそれをパースする
    if (equal(tok, "(") && equal(tok->next, "{")) {
        Node *node = new_node(ND_STMT_EXPR, tok);
        node->body = compound_stmt(&tok, tok->next->next)->body;
        *rest = skip(tok, ")");
        return node;
    }

    // 次のトークンが"("なら、"(" expr ")"のはず
    if (equal(tok, "(")) {
        Node *node = expr(&tok, tok->next);
        *rest = skip(tok, ")");
        return node;
    }

    if (equal(tok, "sizeof") && equal(tok->next, "(") && is_typename(tok->next->next)) {
        Type *ty = typename(&tok, tok->next->next);
        *rest = skip(tok, ")");
        return new_num(ty->size, start);
    }

    // sizeof演算子による演算結果はコンパイル時に決定される
    if (equal(tok, "sizeof")) {
        Node *node = unary(rest, tok->next);
        add_type(node);
        return new_num(node->ty->size, tok);
    }

    // 次に考えられるのは識別子
    if (tok->kind == TK_IDENT) {
        // 後ろに"("があるなら関数呼び出し
        if (equal(tok->next, "("))
            return funcall(rest, tok);

        // 識別子のみの場合は変数
        VarScope *sc = find_var(tok);
        if (!sc || !sc->var)
            error_tok(tok, "未定義な変数です");
        *rest = tok->next;
        return new_var_node(sc->var, tok);
    }

    // 文字列リテラル
    if (tok->kind == TK_STR) {
        Obj *var = new_string_literal(tok->str, tok->ty);
        *rest = tok->next;
        return new_var_node(var, tok);
    }

    // そうでなければ数値のはず
    if (tok->kind == TK_NUM) {
        Node *node = new_num(tok->val, tok);
        *rest = tok->next;
        return node;
    }

    // いずれでもなければそれは式ではない
    error_tok(tok, "式が必要です");
}

// typedefをパースする
static Token *parse_typedef(Token *tok, Type *basety) {
    bool first = true;

    while (!consume(&tok, tok, ";")) {
        if (!first)
            tok = skip(tok, ",");
        first = false;

        Type *ty = declarator(&tok, tok, basety);
        push_scope(get_ident(ty->name))->type_def = ty;
    }
    return tok;
}

static void create_param_lvars(Type *param) {
    if (param) {
        create_param_lvars(param->next);
        new_lvar(get_ident(param->name), param);
    }
}

// function-definitionをパースする
// function-definition = declspec declarator compound_stmt
static Token *function(Token *tok, Type *basety) {
    Type *ty = declarator(&tok, tok, basety);

    Obj *fn = new_gvar(get_ident(ty->name), ty);
    fn->is_function = true;
    fn->is_definition = !consume(&tok, tok, ";");

    if (!fn->is_definition)
        return tok;

    locals = NULL;
    enter_scope();
    create_param_lvars(ty->params);
    fn->params = locals;

    tok = skip(tok, "{");
    fn->body = compound_stmt(&tok, tok);
    fn->locals = locals;
    leave_scope();
    return tok;
}

// global-variableをパースする
// global-variable = declspec declarator ("," declarator) ";"
static Token *global_variable(Token *tok, Type *basety) {
    bool first = true;

    while (!consume(&tok, tok, ";")) {
        if (!first)
            tok = skip(tok, ",");
        first = false;

        Type *ty = declarator(&tok, tok, basety);
        new_gvar(get_ident(ty->name), ty);
    }
    return tok;
}

// トークンを先読みして、与えられたトークンが関数定義か関数宣言を
// 開始するトークンだった場合、trueを返す
static bool is_function(Token *tok) {
    if (equal(tok, ";"))
        return false;

    Type dummy = {};
    Type *ty = declarator(&tok, tok, &dummy);
    return ty->kind == TY_FUNC;
}

// programをパースする
// program = (typedef | function-definition | global-variable)*
Obj *parse(Token *tok) {
    globals = NULL;

    while (tok->kind != TK_EOF) {
        VarAttr attr = {};
        Type *basety = declspec(&tok, tok, &attr);

        // Typedef
        if (attr.is_typedef) {
            tok = parse_typedef(tok, basety);
            continue;
        }

        // 関数
        if (is_function(tok)) {
            tok = function(tok, basety);
            continue;
        }

        // グローバル変数
        tok = global_variable(tok, basety);
    }
    return globals;
}
