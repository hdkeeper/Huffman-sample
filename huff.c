#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NODECNT	256
#define uchar	unsigned char
#define uint	unsigned int
#define ulong	unsigned long

void die(void) {
    fprintf( stderr, "Unexpected end of file\n");
    exit(4);
}

// Per-bit file I/O

typedef struct st_bitfile {
    FILE *file;
    ulong buf;	// Buffer for bits that read
    int cnt;	// Number of bits in the buffer
    int flush;	// 1 if buffer has to be flushed
} BITFILE;

size_t bit_fread( uint *data, size_t count, BITFILE *bf) {
    *data = 0;
    size_t bitread = 0;
    while (count > 0) {
	while ((count > 0) && (bf->cnt > 0)) {
	    uint shift = 8*sizeof(bf->buf) - bf->cnt;
	    *data |= ((bf->buf >> shift) & 1) << bitread;
	    count--;
	    bf->cnt--;
	    bitread++;
	}
	if (bf->cnt == 0) {
	    bf->buf = 0;
	    size_t read;
	    if (read = fread( &(bf->buf), 1, sizeof(bf->buf), bf->file)) {
		bf->buf <<= 8*(sizeof(bf->buf) - read);
		bf->cnt = 8*read;
		bf->flush = 0;
	    }
	    else return bitread;
	}
    }
    return bitread;
}

size_t bit_fwrite( uint *data, size_t count, BITFILE *bf) {
    size_t bitwritten = 0;
    while (count > 0) {
	if (bf->cnt == 8*sizeof(bf->buf)) {
	    if (fwrite( &(bf->buf), 1, sizeof(bf->buf), bf->file)
		< sizeof(bf->buf)) return 0;
	    bf->buf = 0;
	    bf->cnt = 0;
	    bf->flush = 0;
	}
	while ((count > 0) && (bf->cnt < 8*sizeof(bf->buf))) {
	    bf->buf |= ((*data >> bitwritten) & 1) << bf->cnt;
	    count--;
	    bf->cnt++;
	    bf->flush = 1;
	    bitwritten++;
	}
    }
    return bitwritten;
}

BITFILE *bit_fopen( char *path, char *mode) {
    BITFILE *bf = calloc( 1, sizeof(BITFILE));
    if (bf->file = fopen( path, mode))
	return bf;
    else {
	free(bf);
	return NULL;
    }
}

int bit_fclose( BITFILE *bf) {
    int rslt;
    if (bf->flush) {
	rslt = (bf->cnt/8) + ((bf->cnt%8 > 0) ? 1 : 0);
	fwrite( &(bf->buf), 1, rslt, bf->file);
	bf->flush = 0;
    }
    rslt = fclose( bf->file);
    free(bf);
    return rslt;
}

// Binary trees

typedef struct st_node {
    uchar ch;		// 8-bit character in source file
    uint act;		// 1 if the node contains a character
    ulong cnt;		// Number of this characters in source file
    struct st_node *l;
    struct st_node *r;
} NODE;

NODE *nodelist[NODECNT];
int nodecnt;	
NODE *root = NULL;


void initNodelist() {
    int i;
    nodecnt = 256;
    memset( nodelist, 0, sizeof(nodelist));
    for (i=0; i<nodecnt; i++) {
	NODE *t;
	t = calloc( 1, sizeof(NODE));
	t->ch  = (char) i;
	t->act = 1;
	nodelist[i] = t;
    }
}

void readInputStats( FILE *f) {
    uchar buf[4096];
    int i, read;
    rewind(f);
    while (read = fread( buf, 1, sizeof(buf), f)) {
	for (i=0; i<read; i++) {
	    nodelist[ buf[i] ]->cnt += 1;
	}
    }
}

void sortNodelist() {
    int i,j;
    for (i=0; i<nodecnt-1; i++) {
	for (j=i+1; j<nodecnt; j++) {
	    if (nodelist[i]->cnt < nodelist[j]->cnt) {
		NODE *t = nodelist[i];
		nodelist[i] = nodelist[j];
		nodelist[j] = t;
	    }
	}
    }
}

void dropZeroNodes() {
    int i;
    for (i=0; i<nodecnt; i++) {
	if (nodelist[i]->cnt == 0) break;
    }
    nodecnt = i;
}

void buildTree() {
    while (nodecnt > 1) {
	NODE *n1, *n2, *t;
	// Two nodes with least cnt
	n1 = nodelist[nodecnt-1];
	n2 = nodelist[nodecnt-2];
	// Add a center node
	t = calloc( 1, sizeof(NODE));
	t->cnt = n1->cnt + n2->cnt;
	t->l = n1;
	t->r = n2;
	// Replace two nodes with center one
	nodelist[nodecnt-1] = NULL;
	nodelist[nodecnt-2] = t;
	nodecnt--;
	if (nodecnt == 1) break;
	// Pop new center node up
	int i = nodecnt-1;
	while ((i > 0) && (nodelist[i]->cnt > nodelist[i-1]->cnt)) {
	    t = nodelist[i];
	    nodelist[i] = nodelist[i-1];
	    nodelist[i-1] = t;
	    i--;
	}
    }
    root = nodelist[0];
}

void writeTree( NODE *root, BITFILE *bf) {
    bit_fwrite( &(root->act), 1, bf);
    if (root->act) {
	uint ch = root->ch;
	bit_fwrite( &ch, 8, bf);
    } else {
	writeTree( root->l, bf);
	writeTree( root->r, bf);
    }
}

NODE *readTree( BITFILE *bf) {
    NODE *t = calloc( 1, sizeof(NODE));
    if (bit_fread( &(t->act), 1, bf) != 1) die();
    if (t->act) {
	uint ch;
	if (bit_fread( &ch, 8, bf) != 8) die();
	t->ch = ch;
    } else {
	t->l = readTree(bf);
	t->r = readTree(bf);
    }
    return t;
}

// Linear data dictionary

typedef struct st_dict {
    uchar ch;
    ulong cnt;
    uint code;
    uint codelen;
} DICT;

DICT *dict[NODECNT];
int dictlen;


void buildDict( NODE *root, ulong path, ulong pathlen) {
    if (root->act) {
	DICT *t = calloc( 1, sizeof(DICT));
	t->ch = root->ch;
	t->cnt = root->cnt;
	t->code = path;
	t->codelen = pathlen;
	dict[dictlen] = t;
	dictlen++;
    }
    if (root->l)
	// '0' for left branch
	buildDict( root->l, path, pathlen+1);
    if (root->r)
	// '1' for right branch
	buildDict( root->r, path | (1 << pathlen), pathlen+1);
}

void sortDict() {
    int i,j;
    for (i=0; i<dictlen-1; i++) {
	for (j=i+1; j<dictlen; j++) {
	    if (dict[i]->codelen > dict[j]->codelen) {
		DICT *t = dict[i];
		dict[i] = dict[j];
		dict[j] = t;
	    }
	}
    }
}

DICT *getDictByChar( uchar ch) {
    int i, found = 0;
    for (i=0; i < dictlen; i++) {
	if (ch == dict[i]->ch) {
	    found = 1;
	    break;
	}
    }
    return found ? dict[i] : NULL;
}

DICT *getDictByCode( uint code, uint codelen) {
    int i, found = 0;
    // Assumes that dictionary was sorted by sortDict()
    for (i=0; i < dictlen; i++) {
	if (codelen > dict[i]->codelen)
	    continue;
	else if (codelen < dict[i]->codelen)
	    break;
	// codelen == dict[i]->codelen
	else if (code == dict[i]->code) {
	    found = 1;
	    break;
	}
    }
    return found ? dict[i] : NULL;
}

// File processing functions

// Compressed file format:
// 32 bits - source file size
// ?? bits - dictionary tree, see writeTree()
// ?? bits - compressed data, codes from the tree

uint filesize;


void compressData( FILE *f, BITFILE *bf) {
    uchar buf[4096];
    int i, read;
    rewind(f);
    while (read = fread( buf, 1, sizeof(buf), f)) {
	for (i=0; i<read; i++) {
	    DICT *d;
	    if (d = getDictByChar( buf[i]))
	    bit_fwrite( &(d->code), d->codelen, bf);
	}
    }
}

void decompressData( BITFILE *bf, FILE *f) {
    uint buf, bitread = 1;
    uint cnt = 0;
    while (cnt < filesize) {
	uint code, codelen;
	code = codelen = 0;
	DICT *d = NULL;
	while (d == NULL) {
	    if (!bit_fread( &buf, 1, bf)) die();
	    code |= buf << codelen;
	    codelen++;
	    d = getDictByCode( code, codelen);
	}
	fwrite( &(d->ch), 1, 1, f);
	cnt++;
    }
}

ulong getFileSize( char *filename) {
    FILE *f = fopen( filename, "r");
    fseek( f, 0, SEEK_END);
    ulong size = ftell(f);
    fclose(f);
    return size;
}


int main( int argc, char *args[]) {
    if (argc < 3) {
	// Print help
	printf( "Huffman compression and decompression\n"
	"Usage: %s {c|d} <input> <output>\n", args[0] );
	return 5;
    }
    if (args[1][0] == 'c') {
    // PERFORM DATA COMPRESSION
	initNodelist();
	filesize = getFileSize( args[2]);
	// Open input file
	FILE *inf = fopen( args[2], "r");
	if (!inf) return 1;
	// Get statistics
	readInputStats( inf);
	// Sort statistic data
	sortNodelist();
	dropZeroNodes();
	// Combine nodes into binary tree
	buildTree();
	// Make a dictionary table
	dictlen = 0;
	buildDict( root, 0, 0);
	sortDict();
	// Open output file
	BITFILE *outf = bit_fopen( args[3], "w");
	if (!outf) return 2;
	// Store input file size
	bit_fwrite( &filesize, 8*sizeof(filesize), outf);
	// Store binary tree
	writeTree( root, outf);
	// Compress data and store compressed
	compressData( inf, outf);
	// Close all files
	fclose( inf);
	bit_fclose( outf);
	// Print compression ratio
	printf( "Compression ratio: %2.2f %%\n", 100.0 *
	(double) getFileSize(args[3]) / (double) getFileSize(args[2]) );
    } else if (args[1][0] == 'd') {
    // PERFORM DATA DE-COMPRESSION
	// Open input file
	BITFILE *inf = bit_fopen( args[2], "r");
	if (!inf) return 1;
	// Read uncompressed file size
	if (!bit_fread( &filesize, 8*sizeof(filesize), inf)) die();
	// Read binary tree
	root = readTree( inf);
	// Make a dictionary table
	dictlen = 0;
	buildDict( root, 0, 0);
	sortDict();
	// Open output file
	FILE *outf = fopen( args[3], "w");
	if (!outf) return 2;
	// Read compressed data and decompress it
	decompressData( inf, outf);
	// Close all files
	bit_fclose( inf);
	fclose( outf);
    }
    return 0;
}
