#include "chibicc.h"

static int depth;
static char *argreg8[] = {"%dil", "%sil", "%dl", "%cl", "%r8b", "%r9b"};
static char *argreg64[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
static Obj *current_fn;

static void gen_expr(Node *node);

static int count(void) {
    static int i = 1;
    return i++;
}

static void push(void) {
    printf("  push %%rax\n");
    depth++;
}

static void pop(char *arg) {
    printf("  pop %s\n", arg);
    depth--;
}

// nを最も近いalignの倍数に切り上げる。例えば、
// align_to(5, 8)は8を返し、align_to(11, 8)は16を返す
static int align_to(int n, int align) {
    return (n + align - 1) / align * align;
}

// 与えられたノードの絶対アドレスを計算する
// もし与えられたノードがメモリ上に存在しなかったらエラーを出力する
static void gen_addr(Node *node) {
    switch (node->kind) {
    case ND_VAR:
        if (node->var->is_local) {
            // ローカル変数
            printf("  lea %d(%%rbp), %%rax\n", node->var->offset);
        } else {
            // グローバル変数
            printf("  lea %s(%%rip), %%rax\n", node->var->name);
        }
        return;
    case ND_DEREF:
        gen_expr(node->lhs);
        return;
    }

    error_tok(node->tok, "左辺値ではありません");
}

// %raxレジスタの値が指し示しているアドレスから%raxレジスタに値をロードする
static void load(Type *ty) {
    if (ty->kind == TY_ARRAY) {
        // もしそれが配列なら、レジスタに値をロードしようとはしないように。
        // なぜなら、一般的に配列全体を一つのレジスタにロードすることはでき
        // ないからである。結果として、配列を評価した結果は配列そのものでは
        // なく、その配列のアドレスになる。ここでは、「Cにおいて、配列はその
        // 配列の先頭要素へのポインタへと自動的に変換される」ということが起こ
        // っている。
        return;
    }

    if (ty->size == 1)
        printf("  movsbq (%%rax), %%rax\n");
    else
        printf("  mov (%%rax), %%rax\n");
}

// スタックトップの値が指し示しているアドレスに%raxレジスタの値をストアする
static void store(Type *ty) {
    pop("%rdi");

    if (ty->size == 1)
        printf("  mov %%al, (%%rdi)\n");
    else
        printf("  mov %%rax, (%%rdi)\n");
}

// 抽象構文木にしたがって再帰的にアセンブリを出力する
static void gen_expr(Node *node) {
    switch(node->kind) {
    case ND_NUM:
        printf("  mov $%d, %%rax\n", node->val);
        return;
    case ND_NEG:
        gen_expr(node->lhs);
        printf("  neg %%rax\n");
        return;
    case ND_VAR:
        gen_addr(node);
        load(node->ty);
        return;
    case ND_DEREF:
        gen_expr(node->lhs);
        load(node->ty);
        return;
    case ND_ADDR:
        gen_addr(node->lhs);
        return;
    case ND_ASSIGN:
        gen_addr(node->lhs);
        push();
        gen_expr(node->rhs);
        store(node->ty);
        return;
    case ND_FUNCALL: {
        int nargs = 0;
        for (Node *arg = node->args; arg; arg = arg->next) {
            gen_expr(arg);
            push();
            nargs++;
        }

        for (int i = nargs - 1; i >= 0; i--)
            pop(argreg64[i]);

        printf("  mov $0, %%rax\n");
        printf("  call %s\n", node->funcname);
        return;
    }
    }

    gen_expr(node->rhs);
    push();
    gen_expr(node->lhs);
    pop("%rdi");

    switch (node->kind) {
        case ND_ADD:
            printf("  add %%rdi, %%rax\n");
            return;
        case ND_SUB:
            printf("  sub %%rdi, %%rax\n");
            return;
        case ND_MUL:
            printf("  imul %%rdi, %%rax\n");
            return;
        case ND_DIV:
            printf("  cqo\n");
            printf("  idiv %%rdi\n");
            return;
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE:
            printf("  cmp %%rdi, %%rax\n");

            if (node->kind == ND_EQ)
                printf("  sete %%al\n");
            else if (node->kind == ND_NE)
                printf("  setne %%al\n");
            else if (node->kind == ND_LT)
                printf("  setl %%al\n");
            else if (node->kind == ND_LE)
                printf("  setle %%al\n");

            printf("  movzb %%al, %%rax\n");
            return;
    }

    error_tok(node->tok, "正しくない式です");
}

static void gen_stmt(Node *node) {
    switch (node->kind) {
    case ND_IF: {
        int c = count();
        gen_expr(node->cond);
        printf("  cmp $0, %%rax\n");
        printf("  je .L.else.%d\n", c);
        gen_stmt(node->then);
        printf("  jmp .L.end.%d\n", c);
        printf(".L.else.%d:\n", c);
        if (node->els)
            gen_stmt(node->els);
        printf(".L.end.%d:\n", c);
        return;
    }
    case ND_FOR: {
        int c = count();
        if (node->init)
            gen_stmt(node->init);
        printf(".L.begin.%d:\n", c);
        if (node->cond) {
            gen_expr(node->cond);
            printf("  cmp $0, %%rax\n");
            printf("  je .L.end.%d\n", c);
        }
        gen_stmt(node->then);
        if (node->inc)
            gen_expr(node->inc);
        printf("  jmp .L.begin.%d\n", c);
        printf(".L.end.%d:\n", c);
        return;
    }
    case ND_BLOCK:
        // stmtノードを順番に辿ってコード生成
        for (Node *n = node->body; n; n = n->next)
            gen_stmt(n);
        return;
    case ND_RETURN:
        gen_expr(node->lhs);
        // .L.return ラベルにジャンプする
        printf("  jmp .L.return.%s\n", current_fn->name);
        return;
    case ND_EXPR_STMT:
        // expr以下の抽象構文木を下りながらコード生成
        gen_expr(node->lhs);
        return;
    }

    error_tok(node->tok, "正しくない文です");
}

// 各ローカル変数のoffsetにオフセットを代入する
static void assign_lvar_offsets(Obj *prog) {
    for (Obj *fn = prog; fn; fn = fn->next) {
        if (!fn->is_function)
            continue;

        int offset = 0;
        for (Obj *var = fn->locals; var; var = var->next) {
            offset += var->ty->size;
            var->offset = -offset;
        }
        fn->stack_size = align_to(offset, 16);
    }
}

static void emit_data(Obj *prog) {
    for (Obj *var = prog; var; var = var->next) {
        if (var->is_function)
            continue;

        printf("  .data\n");
        printf("  .global %s\n", var->name);
        printf("%s:\n", var->name);
        printf("  .zero %d\n", var->ty->size);
    }
}

static void emit_text(Obj *prog) {
    for (Obj *fn = prog; fn; fn = fn->next) {
        if (!fn->is_function)
            continue;

        printf("  .globl %s\n", fn->name);
        printf("  .text\n");
        printf("%s:\n", fn->name);
        current_fn = fn;

        // プロローグ
        printf("  push %%rbp\n");
        printf("  mov %%rsp, %%rbp\n");
        printf("  sub $%d, %%rsp\n", fn->stack_size);  // 関数フレームの確保

        // レジスタ経由で渡された引数をスタックに保存
        int i = 0;
        for (Obj *var = fn->params; var; var = var->next) {
            if (var->ty->size == 1)
                printf("  mov %s, %d(%%rbp)\n", argreg8[i++], var->offset);
            else
                printf("  mov %s, %d(%%rbp)\n", argreg64[i++], var->offset);
        }

        // コード生成
        gen_stmt(fn->body);
        assert(depth == 0);

        // エピローグ
        printf(".L.return.%s:\n", fn->name);  // return文からの飛び先がここ
        printf("  mov %%rbp, %%rsp\n");
        printf("  pop %%rbp\n");

        // RAX に式を計算した結果が残っているので、
        // それをそのまま返す
        printf("  ret\n");
    }
}

void codegen(Obj *prog) {
    assign_lvar_offsets(prog);
    emit_data(prog);
    emit_text(prog);
}
