#include "9cc.h"

// 新しい二項演算子のノードを作成する
Node *new_node(NodeKind kind, Node *lhs, Node *rhs) {
    Node *node = calloc(1, sizeof(Node));
    node->kind = kind;
    node->lhs = lhs;
    node->rhs = rhs;
    return node;
}

// 新しい数値のノードを作成する
Node *new_node_num(int val) {
    Node *node = calloc(1, sizeof(Node));
    node->kind = ND_NUM;
    node->val = val;
    return node;
}

Node *expr(Token **rest, Token *tok);
Node *equality(Token **rest, Token *tok);
Node *relational(Token **rest, Token *tok);
Node *add(Token **rest, Token *tok);
Node *mul(Token **rest, Token *tok);
Node *unary(Token **rest, Token *tok);
Node *primary(Token **rest, Token *tok);

// exprをパースする
// expr = equality
Node *expr(Token **rest, Token *tok) {
    return equality(rest, tok);
}

// equalityをパースする
// equality = relational ("==" relational | "!=" relational)*
Node *equality(Token **rest, Token *tok) {
    Node *node = relational(&tok, tok);

    for (;;) {
        if (equal(tok, "==")) {
            node = new_node(ND_EQ, node, relational(&tok, tok->next));
            continue;
        }

        if (equal(tok, "!=")) {
            node = new_node(ND_NE, node, relational(&tok, tok->next));
            continue;
        }

        *rest = tok;
        return node;
    }
}

// relationalをパースする
// relational = add ("<" add | "<=" add | ">" add | ">=" add)*
Node *relational(Token **rest, Token *tok) {
    Node *node = add(&tok, tok);

    for (;;) {
        if (equal(tok, "<")) {
            node = new_node(ND_LT, node, add(&tok, tok->next));
            continue;
        }

        if (equal(tok, "<=")) {
            node = new_node(ND_LE, node, add(&tok, tok->next));
            continue;
        }

        if (equal(tok, ">")) {
            node = new_node(ND_LT, add(&tok, tok->next), node);
            continue;
        }

        if (equal(tok, ">=")) {
            node = new_node(ND_LE, add(&tok, tok->next), node);
            continue;
        }

        *rest = tok;
        return node;
    }
}

// addをパースする
// add = mul ("+" mul | "-" mul)*
Node *add(Token **rest, Token *tok) {
    Node *node = mul(&tok, tok);

    for (;;) {
        if (equal(tok, "+")) {
            node = new_node(ND_ADD, node, mul(&tok, tok->next));
            continue;
        }

        if (equal(tok, "-")) {
            node = new_node(ND_SUB, node, mul(&tok, tok->next));
            continue;
        }

        *rest = tok;
        return node;
    }
}

// mulをパースする
// mul = unary ("*" unary | "/" unary)*
Node *mul(Token **rest, Token *tok) {
    Node *node = unary(&tok, tok);

    for (;;) {
        if (equal(tok, "*")) {
            node = new_node(ND_MUL, node, unary(&tok, tok->next));
            continue;
        }

        if (equal(tok, "/")) {
            node = new_node(ND_DIV, node, unary(&tok, tok->next));
            continue;
        }

        *rest = tok;
        return node;
    }
}

// unaryをパースする
// unary = ("+" | "-")? unary | primary
Node *unary(Token **rest, Token *tok) {
    if (equal(tok, "+"))
        return unary(rest, tok->next);
    if (equal(tok, "-"))
        return new_node(ND_SUB, new_node_num(0), unary(rest, tok->next));
    return primary(rest, tok);
}

// primaryをパースする
// primary = num | "(" expr ")"
Node *primary(Token **rest, Token *tok) {
    // 次のトークンが"("なら、"(" expr ")"のはず
    if (equal(tok, "(")) {
        Node *node = expr(&tok, tok->next);
        expect(rest, tok, ")");
        return node;
    }

    // そうでなければ数値のはず
    return new_node_num(expect_number(rest, tok));
}

Node *parse(Token *tok) {
    Node *node = expr(&tok, tok);
    if (tok->kind != TK_EOF)
        error_tok(tok, "余分なトークンです");
    return node;
}