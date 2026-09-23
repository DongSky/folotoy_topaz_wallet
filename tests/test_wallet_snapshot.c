// Integration oracle: consume an actual Android-produced PCW1 file. This checks
// the independent C decoder, rather than round-tripping with the same encoder.
#include "wallet_core.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    FILE *input = fopen(argv[1], "rb");
    if (!input) return 2;
    uint8_t header[WALLET_HEADER_SIZE];
    wallet_header_t decoded;
    if (fread(header, 1, sizeof(header), input) != sizeof(header) ||
        !wallet_header_parse(header, sizeof(header), &decoded)) {
        fclose(input);
        return 1;
    }
    uint32_t crc = 0;
    uint8_t *page = malloc(WALLET_PAGE_RECORD_SIZE);
    if (!page) { fclose(input); return 2; }
    unsigned kinds[WALLET_KIND_COUNT] = {0};
    int result = 0;
    for (unsigned index = 0; index < decoded.page_count; index++) {
        if (fread(page, 1, WALLET_PAGE_RECORD_SIZE, input) != WALLET_PAGE_RECORD_SIZE ||
            !wallet_page_valid(page) || (index == 0 && page[0] != 0)) {
            result = 1;
            break;
        }
        for (unsigned i = 0; i < 16; i++) {
            if (page[4 + i * 4 + 3] != 255) result = 1;
        }
        if (result) break;
        kinds[page[0]]++;
        crc = wallet_crc32(crc, page, WALLET_PAGE_RECORD_SIZE);
    }
    if (crc != decoded.body_crc || kinds[0] != 1 || fgetc(input) != EOF) result = 1;
    free(page);
    fclose(input);
    if (!result) printf("PCW1 accepted: %u pages, %u card, %u social, %u payment, %u crypto, %u assets\n",
        decoded.page_count, kinds[0], kinds[1], kinds[2], kinds[3], kinds[4]);
    return result;
}
