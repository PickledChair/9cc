#include "9cc.h"

// パースしている間に作成されたすべてのローカル変数インスタンスは
// この連結リストに積み重ねられていく
Obj *locals;

// ローカル変数を名前によって探す
static Obj *find_var(Token *tok) {
    for (Obj *var = locals; var; var = var->next)
        if (strlen(var->name) == tok->len && !strncmp(tok->loc, var->name, tok->len))
            return var;
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
static Node *new_num(int val, Token *tok) {
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

// 新しいローカル変数を作成する
static Obj *new_lvar(char *name) {
    Obj *var = calloc(1, sizeof(Obj));
    var->name = name;
    var->next = locals;
    locals = var;
    return var;
}

static Node *stmt(Token **rest, Token *tok);
static Node *compound_stmt(Token **rest, Token *tok);
static Node *expr_stmt(Token **rest, Token *tok);
static Node *assign(Token **rest, Token *tok);
static Node *expr(Token **rest, Token *tok);
static Node *equality(Token **rest, Token *tok);
static Node *relational(Token **rest, Token *tok);
static Node *add(Token **rest, Token *tok);
static Node *mul(Token **rest, Token *tok);
static Node *unary(Token **rest, Token *tok);
static Node *primary(Token **rest, Token *tok);

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
// compound-stmt = stmt* "}"
static Node *compound_stmt(Token **rest, Token *tok) {
    Node *node = new_node(ND_BLOCK, tok);

    Node head = {};
    Node *cur = &head;
    while (!equal(tok, "}"))
        cur = cur->next = stmt(&tok, tok);

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
// expr = assign
static Node *expr(Token **rest, Token *tok) {
    return assign(rest, tok);
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

// addをパースする
// add = mul ("+" mul | "-" mul)*
static Node *add(Token **rest, Token *tok) {
    Node *node = mul(&tok, tok);

    for (;;) {
        Token *start = tok;

        if (equal(tok, "+")) {
            node = new_binary(ND_ADD, node, mul(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, "-")) {
            node = new_binary(ND_SUB, node, mul(&tok, tok->next), start);
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
// unary = ("+" | "-" | "*" | "&")? unary | primary
static Node *unary(Token **rest, Token *tok) {
    if (equal(tok, "+"))
        return unary(rest, tok->next);
    if (equal(tok, "-"))
        return new_unary(ND_NEG, unary(rest, tok->next), tok);
    if (equal(tok, "&"))
        return new_unary(ND_ADDR, unary(rest, tok->next), tok);
    if (equal(tok, "*"))
        return new_unary(ND_DEREF, unary(rest, tok->next), tok);
    return primary(rest, tok);
}

// primaryをパースする
// primary = "(" expr ")" | ident | num
static Node *primary(Token **rest, Token *tok) {
    // 次のトークンが"("なら、"(" expr ")"のはず
    if (equal(tok, "(")) {
        Node *node = expr(&tok, tok->next);
        *rest = skip(tok, ")");
        return node;
    }

    // 次に考えられるのは識別子
    if (tok->kind == TK_IDENT) {
        Obj *var = find_var(tok);
        if (!var)
            var = new_lvar(strndup(tok->loc, tok->len));
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

// programは複数のstmtからなる
// program = stmt*
Function *parse(Token *tok) {
    tok = skip(tok, "{");
    
    Function *prog = calloc(1, sizeof(Function));
    prog->body = compound_stmt(&tok, tok);
    prog->locals = locals;
    return prog;
}