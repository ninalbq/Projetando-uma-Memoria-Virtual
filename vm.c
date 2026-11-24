#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <stdint.h>

#define PAGE_SIZE   256
#define NUM_PAGES   256
#define NUM_FRAMES  128
#define TLB_SIZE    16
#define BACKING     "BACKING_STORE.bin"
#define OUTPUT      "correct.txt"
#define MAX_ADDR    65535

typedef struct { int valid, page, frame; long loaded, used; } TlbEntry;

static TlbEntry tlb[TLB_SIZE];
static int page_table[NUM_PAGES];
static int frame_page[NUM_FRAMES];
static long frame_loaded[NUM_FRAMES], frame_used[NUM_FRAMES];
static signed char memory[NUM_FRAMES][PAGE_SIZE];
static int lookup_page;
static int found[TLB_SIZE];

static void *tlb_search(void *arg) {
    int i = (int)(intptr_t)arg;
    found[i] = (tlb[i].valid && tlb[i].page == lookup_page);
    return NULL;
}

static int tlb_lookup(int page) {
    pthread_t th[TLB_SIZE];
    int created[TLB_SIZE], res = -1;
    lookup_page = page;
    for (int i = 0; i < TLB_SIZE; i++) {
        found[i] = 0;
        created[i] = (pthread_create(&th[i], NULL, tlb_search, (void *)(intptr_t)i) == 0);
        if (!created[i]) tlb_search((void *)(intptr_t)i);
    }
    for (int i = 0; i < TLB_SIZE; i++) if (created[i]) pthread_join(th[i], NULL);
    for (int i = 0; i < TLB_SIZE; i++) if (found[i]) res = i;
    return res;
}

static int tlb_insert(int page, int frame, int lru, long now) {
    int slot = -1;
    for (int i = 0; i < TLB_SIZE && slot < 0; i++) if (!tlb[i].valid) slot = i;
    if (slot < 0) {
        slot = 0;
        for (int i = 1; i < TLB_SIZE; i++) {
            long a = lru ? tlb[i].used : tlb[i].loaded;
            long b = lru ? tlb[slot].used : tlb[slot].loaded;
            if (a < b) slot = i;
        }
    }
    tlb[slot] = (TlbEntry){1, page, frame, now, now};
    return slot;
}

static void tlb_invalidate(int page) {
    for (int i = 0; i < TLB_SIZE; i++) if (tlb[i].valid && tlb[i].page == page) tlb[i].valid = 0;
}

static int usage(const char *p) {
    fprintf(stderr, "Erro: uso correto: %s <arquivo_enderecos> <fifo|lru> <fifo|lru>\n", p);
    return 1;
}

static int parse_algo(const char *s) {
    if (strcmp(s, "fifo") == 0) return 0;
    if (strcmp(s, "lru") == 0) return 1;
    return -1;
}

static int *read_addresses(const char *path, int *count) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "Erro: nao foi possivel abrir o arquivo de enderecos '%s'.\n", path); return NULL; }
    int cap = 1024, n = 0, *v = malloc(cap * sizeof(int));
    char line[256];
    int lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '\0') continue;
        char *e = p; long val = 0; int digits = 0;
        while (isdigit((unsigned char)*e)) { val = val * 10 + (*e - '0'); digits++; e++; if (val > MAX_ADDR) break; }
        while (isspace((unsigned char)*e)) e++;
        if (digits == 0 || *e != '\0' || val > MAX_ADDR) {
            fprintf(stderr, "Erro: formato invalido na linha %d de '%s' (esperado inteiro entre 0 e %d).\n", lineno, path, MAX_ADDR);
            free(v); fclose(f); return NULL;
        }
        if (n == cap) { cap *= 2; v = realloc(v, cap * sizeof(int)); }
        v[n++] = (int)val;
    }
    fclose(f);
    if (n == 0) { fprintf(stderr, "Erro: o arquivo '%s' esta vazio.\n", path); free(v); return NULL; }
    *count = n;
    return v;
}

int main(int argc, char **argv) {
    if (argc != 4) return usage(argv[0]);
    int page_lru = parse_algo(argv[2]), tlb_lru = parse_algo(argv[3]);
    if (page_lru < 0 || tlb_lru < 0) {
        fprintf(stderr, "Erro: algoritmo invalido ('%s', '%s'). Use fifo ou lru.\n", argv[2], argv[3]);
        return 1;
    }
    int n;
    int *addrs = read_addresses(argv[1], &n);
    if (!addrs) return 1;

    FILE *bs = fopen(BACKING, "rb");
    if (!bs) { fprintf(stderr, "Erro: nao foi possivel abrir '%s'.\n", BACKING); free(addrs); return 1; }
    FILE *out = fopen(OUTPUT, "w");
    if (!out) { fprintf(stderr, "Erro: nao foi possivel criar '%s'.\n", OUTPUT); fclose(bs); free(addrs); return 1; }

    for (int i = 0; i < NUM_PAGES; i++) page_table[i] = -1;
    for (int i = 0; i < NUM_FRAMES; i++) frame_page[i] = -1;

    long faults = 0, hits = 0, now = 0;
    int next_free = 0;

    for (int k = 0; k < n; k++) {
        now++;
        int addr = addrs[k], page = (addr >> 8) & 0xFF, off = addr & 0xFF;
        int frame, shown;
        int idx = tlb_lookup(page);
        if (idx >= 0) {
            hits++; frame = tlb[idx].frame; tlb[idx].used = now; shown = idx;
            frame_used[frame] = now;
        } else {
            if (page_table[page] >= 0) {
                frame = page_table[page];
                frame_used[frame] = now;
            } else {
                faults++;
                if (next_free < NUM_FRAMES) frame = next_free++;
                else {
                    frame = 0;
                    for (int i = 1; i < NUM_FRAMES; i++) {
                        long a = page_lru ? frame_used[i] : frame_loaded[i];
                        long b = page_lru ? frame_used[frame] : frame_loaded[frame];
                        if (a < b) frame = i;
                    }
                    page_table[frame_page[frame]] = -1;
                    tlb_invalidate(frame_page[frame]);
                }
                if (fseek(bs, (long)page * PAGE_SIZE, SEEK_SET) != 0 ||
                    fread(memory[frame], 1, PAGE_SIZE, bs) != PAGE_SIZE) {
                    fprintf(stderr, "Erro: falha ao ler a pagina %d de '%s'.\n", page, BACKING);
                    fclose(bs); fclose(out); free(addrs); return 1;
                }
                page_table[page] = frame; frame_page[frame] = page;
                frame_loaded[frame] = frame_used[frame] = now;
            }
            shown = tlb_insert(page, frame, tlb_lru, now);
        }
        fprintf(out, "Virtual address: %d TLB: %d Physical address: %d Value: %d\n",
                addr, shown, frame * PAGE_SIZE + off, memory[frame][off]);
    }
    fprintf(out, "Number of Translated Addresses = %d\n", n);
    fprintf(out, "Page Faults = %ld\n", faults);
    fprintf(out, "Page Fault Rate = %.3f\n", (double)faults / n);
    fprintf(out, "TLB Hits = %ld\n", hits);
    fprintf(out, "TLB Hit Rate = %.3f\n", (double)hits / n);
    fclose(bs); fclose(out); free(addrs);
    return 0;
}
