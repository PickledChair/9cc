#include "chibicc.h"

static FILE *output_file;
static int depth;
static char *argreg8[] = {"%dil", "%sil", "%dl", "%cl", "%r8b", "%r9b"};
static char *argreg32[] = {"%edi", "%esi", "%edx", "%ecx", "%r8d", "%r9d"};
static char *argreg64[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
static Obj *current_fn;

static void gen_expr(Node *node);
static void gen_stmt(Node *node);

static void println(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(output_file, fmt, ap);
    va_end(ap);
    fprintf(output_file, "\n");
}

static int count(void) {
    static int i = 1;
    return i++;
}

static void push(void) {
    println("  push %%rax");
    depth++;
}

static void pop(char *arg) {
    println("  pop %s", arg);
    depth--;
}

// nを最も近いalignの倍数に切り上げる。例えば、
// align_to(5, 8)は8を返し、align_to(11, 8)は16を返す
int align_to(int n, int align) {
    return (n + align - 1) / align * align;
}

// 与えられたノードの絶対アドレスを計算する
// もし与えられたノードがメモリ上に存在しなかったらエラーを出力する
static void gen_addr(Node *node) {
    switch (node->kind) {
    case ND_VAR:
        if (node->var->is_local) {
            // ローカル変数
            println("  lea %d(%%rbp), %%rax", node->var->offset);
        } else {
            // グローバル変数
            println("  lea %s(%%rip), %%rax", node->var->name);
        }
        return;
    case ND_DEREF:
        gen_expr(node->lhs);
        return;
    case ND_COMMA:
        gen_expr(node->lhs);
        gen_addr(node->rhs);
        return;
    case ND_MEMBER:
        gen_addr(node->lhs);
        println("  add $%d, %%rax", node->member->offset);
        return;
    }

    error_tok(node->tok, "左辺値ではありません");
}

// %raxレジスタの値が指し示しているアドレスから%raxレジスタに値をロードする
static void load(Type *ty) {
    if (ty->kind == TY_ARRAY || ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
        // もしそれが配列なら、レジスタに値をロードしようとはしないように。
        // なぜなら、一般的に配列全体を一つのレジスタにロードすることはでき
        // ないからである。結果として、配列を評価した結果は配列そのものでは
        // なく、その配列のアドレスになる。ここでは、「Cにおいて、配列はその
        // 配列の先頭要素へのポインタへと自動的に変換される」ということが起こ
        // っている。
        return;
    }

    if (ty->size == 1)
        println("  movsbq (%%rax), %%rax");
    else if (ty->size == 4)
        println("  movsxd (%%rax), %%rax");
    else
        println("  mov (%%rax), %%rax");
}

// スタックトップの値が指し示しているアドレスに%raxレジスタの値をストアする
static void store(Type *ty) {
    pop("%rdi");

    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
        for (int i = 0; i < ty->size; i++) {
            println("  mov %d(%%rax), %%r8b", i);
            println("  mov %%r8b, %d(%%rdi)", i);
        }
        return;
    }

    if (ty->size == 1)
        println("  mov %%al, (%%rdi)");
    else if (ty->size == 4)
        println("  mov %%eax, (%%rdi)");
    else
        println("  mov %%rax, (%%rdi)");
}

// 抽象構文木にしたがって再帰的にアセンブリを出力する
static void gen_expr(Node *node) {
    println("  .loc 1 %d", node->tok->line_no);

    switch(node->kind) {
    case ND_NUM:
        println("  mov $%ld, %%rax", node->val);
        return;
    case ND_NEG:
        gen_expr(node->lhs);
        println("  neg %%rax");
        return;
    case ND_VAR:
    case ND_MEMBER:
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
    case ND_STMT_EXPR:
        for (Node *n = node->body; n; n = n->next)
            gen_stmt(n);
        return;
    case ND_COMMA:
        gen_expr(node->lhs);
        gen_expr(node->rhs);
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

        println("  mov $0, %%rax");
        println("  call %s", node->funcname);
        return;
    }
    }

    gen_expr(node->rhs);
    push();
    gen_expr(node->lhs);
    pop("%rdi");

    switch (node->kind) {
        case ND_ADD:
            println("  add %%rdi, %%rax");
            return;
        case ND_SUB:
            println("  sub %%rdi, %%rax");
            return;
        case ND_MUL:
            println("  imul %%rdi, %%rax");
            return;
        case ND_DIV:
            println("  cqo");
            println("  idiv %%rdi");
            return;
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE:
            println("  cmp %%rdi, %%rax");

            if (node->kind == ND_EQ)
                println("  sete %%al");
            else if (node->kind == ND_NE)
                println("  setne %%al");
            else if (node->kind == ND_LT)
                println("  setl %%al");
            else if (node->kind == ND_LE)
                println("  setle %%al");

            println("  movzb %%al, %%rax");
            return;
    }

    error_tok(node->tok, "正しくない式です");
}

static void gen_stmt(Node *node) {
    println("  .loc 1 %d", node->tok->line_no);

    switch (node->kind) {
    case ND_IF: {
        int c = count();
        gen_expr(node->cond);
        println("  cmp $0, %%rax");
        println("  je .L.else.%d", c);
        gen_stmt(node->then);
        println("  jmp .L.end.%d", c);
        println(".L.else.%d:", c);
        if (node->els)
            gen_stmt(node->els);
        println(".L.end.%d:", c);
        return;
    }
    case ND_FOR: {
        int c = count();
        if (node->init)
            gen_stmt(node->init);
        println(".L.begin.%d:", c);
        if (node->cond) {
            gen_expr(node->cond);
            println("  cmp $0, %%rax");
            println("  je .L.end.%d", c);
        }
        gen_stmt(node->then);
        if (node->inc)
            gen_expr(node->inc);
        println("  jmp .L.begin.%d", c);
        println(".L.end.%d:", c);
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
        println("  jmp .L.return.%s", current_fn->name);
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
            offset = align_to(offset, var->ty->align);
            var->offset = -offset;
        }
        fn->stack_size = align_to(offset, 16);
    }
}

static void emit_data(Obj *prog) {
    for (Obj *var = prog; var; var = var->next) {
        if (var->is_function)
            continue;

        println("  .data");
        println("  .global %s", var->name);
        println("%s:", var->name);

        if (var->init_data) {
            for (int i = 0; i < var->ty->size; i++)
                println("  .byte %d", var->init_data[i]);
        } else {
            println("  .zero %d", var->ty->size);
        }
    }
}

static void store_gp(int r, int offset, int sz) {
    switch (sz) {
    case 1:
        println("  mov %s, %d(%%rbp)", argreg8[r], offset);
        return;
    case 4:
        println("  mov %s, %d(%%rbp)", argreg32[r], offset);
        return;
    case 8:
        println("  mov %s, %d(%%rbp)", argreg64[r], offset);
        return;
    }
    unreachable();
}

static void emit_text(Obj *prog) {
    for (Obj *fn = prog; fn; fn = fn->next) {
        if (!fn->is_function)
            continue;

        println("  .globl %s", fn->name);
        println("  .text");
        println("%s:", fn->name);
        current_fn = fn;

        // プロローグ
        println("  push %%rbp");
        println("  mov %%rsp, %%rbp");
        println("  sub $%d, %%rsp", fn->stack_size);  // 関数フレームの確保

        // レジスタ経由で渡された引数をスタックに保存
        int i = 0;
        for (Obj *var = fn->params; var; var = var->next)
            store_gp(i++, var->offset, var->ty->size);

        // コード生成
        gen_stmt(fn->body);
        assert(depth == 0);

        // エピローグ
        println(".L.return.%s:", fn->name);  // return文からの飛び先がここ
        println("  mov %%rbp, %%rsp");
        println("  pop %%rbp");

        // RAX に式を計算した結果が残っているので、
        // それをそのまま返す
        println("  ret");
    }
}

void codegen(Obj *prog, FILE *out) {
    output_file = out;

    assign_lvar_offsets(prog);
    emit_data(prog);
    emit_text(prog);
}
