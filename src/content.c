#include "content.h"
#include "file.h"
#include "jar.h"
#include "port.h"
#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <wincodec.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PICTURE_MAX_DIMENSION 128
#define PICTURE_HEADER_SIZE 11

static int fail(ContentImport *im, const char *reason)
{
    snprintf(im->error, sizeof(im->error), "Content import: %s", reason);
    im->failed = 1; return 0;
}
/* Both host decoders produce the container read by the guest's Draw_Image.
   The nine-byte animation header declares one frame. Its length includes
   two zero bytes (raw bitmap, no palette) before the little-endian RGB565 pixels. */
static uint8_t *picture_buffer(unsigned width, unsigned height, unsigned pixel_bytes)
{
    uint8_t *bitmap = calloc(1, PICTURE_HEADER_SIZE + pixel_bytes);
    unsigned frame_bytes = pixel_bytes + 2;

    if (!bitmap) return NULL;
    bitmap[0] = (uint8_t)width;
    bitmap[1] = (uint8_t)height;
    bitmap[3] = 1;
    bitmap[5] = (uint8_t)frame_bytes;
    bitmap[6] = (uint8_t)(frame_bytes >> 8);
    return bitmap;
}

#ifdef _WIN32
static int picture(ContentImport *im)
{
    IWICImagingFactory *factory=NULL;IWICStream *stream=NULL;
    IWICBitmapDecoder *decoder=NULL;IWICBitmapFrameDecode *frame=NULL;
    IWICBitmapScaler *scaler=NULL;IWICFormatConverter *converter=NULL;
    IWICBitmapSource *source=NULL;uint8_t *bitmap=NULL;UINT w=0,h=0;unsigned size=0;
    HRESULT init=CoInitializeEx(NULL,COINIT_MULTITHREADED),hr;int ok=0;
    if(FAILED(init)&&init!=RPC_E_CHANGED_MODE)return fail(im,"cannot initialize the image decoder");
    hr=CoCreateInstance(&CLSID_WICImagingFactory,NULL,CLSCTX_INPROC_SERVER,&IID_IWICImagingFactory,(void **)&factory);
    if(FAILED(hr))goto done;
    if(FAILED(IWICImagingFactory_CreateStream(factory,&stream))||
       FAILED(IWICStream_InitializeFromMemory(stream,im->file,im->size))||
       FAILED(IWICImagingFactory_CreateDecoderFromStream(factory,(IStream *)stream,NULL,WICDecodeMetadataCacheOnLoad,&decoder))||
       FAILED(IWICBitmapDecoder_GetFrame(decoder,0,&frame))||
       FAILED(IWICBitmapFrameDecode_GetSize(frame,&w,&h))||!w||!h)goto done;
    source=(IWICBitmapSource *)frame;
    if(w>PICTURE_MAX_DIMENSION||h>PICTURE_MAX_DIMENSION) {
        if(w>=h){h=(UINT)((uint64_t)h*PICTURE_MAX_DIMENSION/w);w=PICTURE_MAX_DIMENSION;}
        else {w=(UINT)((uint64_t)w*PICTURE_MAX_DIMENSION/h);h=PICTURE_MAX_DIMENSION;}
        if(!w)w=1;if(!h)h=1;
        if(FAILED(IWICImagingFactory_CreateBitmapScaler(factory,&scaler))||
           FAILED(IWICBitmapScaler_Initialize(scaler,source,w,h,WICBitmapInterpolationModeFant)))goto done;
        source=(IWICBitmapSource *)scaler;
    }
    if(FAILED(IWICImagingFactory_CreateFormatConverter(factory,&converter))||
       FAILED(IWICFormatConverter_Initialize(converter,source,&GUID_WICPixelFormat16bppBGR565,
           WICBitmapDitherTypeNone,NULL,0,WICBitmapPaletteTypeCustom)))goto done;
    size = w * h * 2;
    bitmap = picture_buffer(w, h, size);
    if (!bitmap) goto done;
    if (FAILED(IWICFormatConverter_CopyPixels(converter, NULL, w * 2, size, bitmap + PICTURE_HEADER_SIZE)))
        goto done;
    free(im->file);im->file=bitmap;bitmap=NULL;im->size=size+PICTURE_HEADER_SIZE;im->type=7;ok=1;
done:
    free(bitmap);
    if(converter)IWICFormatConverter_Release(converter);
    if(scaler)IWICBitmapScaler_Release(scaler);
    if(frame)IWICBitmapFrameDecode_Release(frame);
    if(decoder)IWICBitmapDecoder_Release(decoder);
    if(stream)IWICStream_Release(stream);
    if(factory)IWICImagingFactory_Release(factory);
    if(SUCCEEDED(init))CoUninitialize();
    return ok?1:fail(im,"cannot decode this picture");
}
#else
/* Decode uncompressed 24- and 32-bit BMP files on Linux. */
static int picture(ContentImport *im)
{
    const uint8_t *bmp = im->file;
    uint32_t pixel_offset, header_size, width, height, bits_per_pixel, bytes_per_pixel;
    uint32_t target_width, target_height, pixel_bytes;
    uint64_t stride;
    int top_down;
    uint8_t *bitmap;
    uint32_t x, y;

    /* Require the 14-byte BMP file header and at least a 40-byte DIB header. */
    if (im->size < 54 || bmp[0] != 'B' || bmp[1] != 'M')
        return fail(im, "cannot decode this picture");
    pixel_offset = le32(bmp + 10);
    header_size = le32(bmp + 14);
    width = le32(bmp + 18);
    height = le32(bmp + 22);
    bits_per_pixel = bmp[28] | (bmp[29] << 8);
    if (header_size < 40 || pixel_offset < 54 || pixel_offset > im->size ||
        header_size > pixel_offset - 14 || !width || width > INT32_MAX || !height ||
        bmp[26] != 1 || bmp[27] != 0 || (bits_per_pixel != 24 && bits_per_pixel != 32) ||
        le32(bmp + 30) != 0)
        return fail(im, "cannot decode this picture");
    /* A negative height means the file already stores rows top to bottom. */
    top_down = (height & 0x80000000u) != 0;
    if (top_down)
        height = 0u - height;
    /* Rows are padded to a multiple of four bytes. Check that they all fit without
       multiplying stride by an untrusted height. */
    bytes_per_pixel = bits_per_pixel / 8;
    stride = ((uint64_t)width * bytes_per_pixel + 3u) & ~UINT64_C(3);
    if (stride > (im->size - pixel_offset) / height)
        return fail(im, "cannot decode this picture");

    /* Fit the phone's LCD, keeping at least one pixel on each axis. */
    target_width = width;
    target_height = height;
    if (width > PICTURE_MAX_DIMENSION || height > PICTURE_MAX_DIMENSION) {
        if (width >= height) {
            target_height = (uint32_t)((uint64_t)height * PICTURE_MAX_DIMENSION / width);
            target_width = PICTURE_MAX_DIMENSION;
        } else {
            target_width = (uint32_t)((uint64_t)width * PICTURE_MAX_DIMENSION / height);
            target_height = PICTURE_MAX_DIMENSION;
        }
        if (!target_width) target_width = 1;
        if (!target_height) target_height = 1;
    }

    pixel_bytes = target_width * target_height * 2;
    bitmap = picture_buffer(target_width, target_height, pixel_bytes);
    if (!bitmap)
        return fail(im, "cannot allocate the bitmap buffer");

    /* Pick the source pixel for each output position, dropping BMP alpha. */
    for (y = 0; y < target_height; ++y) {
        uint32_t row = (uint32_t)((uint64_t)y * height / target_height);
        if (!top_down)
            row = height - 1 - row;
        for (x = 0; x < target_width; ++x) {
            uint32_t column = (uint32_t)((uint64_t)x * width / target_width);
            const uint8_t *pixel = bmp + pixel_offset + row * stride + column * bytes_per_pixel;
            uint8_t *output = bitmap + PICTURE_HEADER_SIZE + (y * target_width + x) * 2;
            uint16_t red = pixel[2] >> 3, green = pixel[1] >> 2, blue = pixel[0] >> 3;
            uint16_t rgb565 = (uint16_t)((red << 11) | (green << 5) | blue);
            output[0] = (uint8_t)rgb565;
            output[1] = (uint8_t)(rgb565 >> 8);
        }
    }

    free(im->file);
    im->file = bitmap;
    im->size = PICTURE_HEADER_SIZE + pixel_bytes;
    im->type = 7;
    return 1;
}
#endif
/* NLT slot names are at most 11 bytes plus the terminator. Keep only bytes the
   filesystem uses for its own entries so a host file name cannot smuggle a path
   or control character into the catalog. */
static void derive_stem(const char *path, char out[CONTENT_NAME_MAX])
{
    const char *base = path, *p, *fallback = "Imported";
    unsigned n = 0;
    for (p = path; *p; ++p) if (*p == '/' || *p == '\\') base = p + 1;
    for (p = base; *p && *p != '.' && n < CONTENT_NAME_MAX - 1; ++p) {
        unsigned char c = (unsigned char)*p;
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            c == '_' || c == '-') out[n++] = (char)c;
    }
    if (!n) for (p = fallback; *p && n < CONTENT_NAME_MAX - 1; ++p) out[n++] = *p;
    out[n] = 0;
}
/* Attempt 0 is the plain stem; later attempts append a short decimal suffix so a
   repeated import of the same file gets its own slot instead of colliding. */
static void make_name(ContentImport *im, unsigned attempt)
{
    char suffix[4];
    size_t keep, len;
    if (!attempt) { strcpy(im->name, im->stem); return; }
    snprintf(suffix, sizeof(suffix), "%u", attempt % 100);
    keep = (CONTENT_NAME_MAX - 1) - strlen(suffix);
    len = strlen(im->stem); if (len > keep) len = keep;
    memcpy(im->name, im->stem, len);
    strcpy(im->name + len, suffix);
}
static int poke(Machine *m, uint32_t address, const uint8_t *bytes, uint32_t count)
{
    uint32_t i;
    m->bus.fault = 0;
    for (i = 0; i < count; ++i) bus_write(&m->bus, address + i, 1, bytes[i]);
    if (m->bus.fault) { m->bus.fault = 0; return 0; }
    return 1;
}
static int name_into_guest(Machine *m, ContentImport *im)
{
    return poke(m, im->name_ptr, (const uint8_t *)im->name, (uint32_t)strlen(im->name) + 1);
}
/* flash_aWriteByName takes four registers plus three stacked words (the result
   ticket and two unused callback fields); the guest writer keeps the data
   pointer, so the buffer stays live for the asynchronous flash task. */
static int stack_args(Machine *m, ContentImport *im)
{
    m->bus.fault = 0;
    bus_write(&m->bus, im->stack, 4, im->out);
    bus_write(&m->bus, im->stack + 4, 4, 0);
    bus_write(&m->bus, im->stack + 8, 4, 0);
    if (m->bus.fault) { m->bus.fault = 0; return 0; }
    return 1;
}
/* Resume the interrupted guest exactly where it stood, but keep the instruction
   and cycle counters the injected calls consumed. */
static void restore_caller(ContentImport *im, Machine *m)
{
    uint64_t instructions = m->cpu.instructions, cycles = m->cpu.cycles, clocks = m->cpu.clock_cycles;
    m->cpu = im->caller;
    m->cpu.instructions = instructions; m->cpu.cycles = cycles; m->cpu.clock_cycles = clocks;
}
int content_import_open(ContentImport *im, Machine *m, const char *path)
{
    size_t n = 0;
    const char *ext = strrchr(path, '.');
    memset(im, 0, sizeof(*im));
    if (!m->import_trigger || !m->import_alloc || !m->import_search || !m->import_write)
        return fail(im, "model declares no import guest entries");
    if (ext && !plat_stricmp(ext, ".jar")) {
        JarPackage jar = {0};
        if (!m->java_ready)
            return fail(im, "model declares no Java installer entries");
        if (!jar_open(&jar, path)) {
            snprintf(im->error, sizeof(im->error), "%s", jar.error);
            im->failed = 1;
            return 0;
        }
        im->file = jar.archive;
        im->size = (uint32_t)jar.size;
        im->jad = jar.jad;
        im->type = 10;
        im->trigger = m->java_ready;
        im->alloc = m->import_alloc;
        derive_stem(path, im->stem);
        strcpy(im->name, im->stem);
        im->active = 1;
        return 1;
    }
    if (ext && !plat_stricmp(ext, ".jad"))
        return fail(im, "select the JAR; its descriptor is generated from the manifest");
    if (!file_read(path, &im->file, &n)) return fail(im, "cannot read the input file");
    if (!n) { free(im->file); im->file = NULL; return fail(im, "the input file is empty"); }
    if (n > 16*1024*1024) { free(im->file); im->file = NULL; return fail(im, "the input file exceeds 16 MiB"); }
    im->size = (uint32_t)n;
    im->type=6;
    if (ext && (!plat_stricmp(ext, ".png") || !plat_stricmp(ext, ".jpg") ||
                !plat_stricmp(ext, ".jpeg") || !plat_stricmp(ext, ".gif") ||
                !plat_stricmp(ext, ".bmp"))) {
        if (!picture(im)) {
            free(im->file);
            im->file = NULL;
            return 0;
        }
    }
    /* ReadRecordData converts the requested byte length to halfwords with
       length >> 1. The native download path therefore needs an even stored
       length: otherwise an odd SMAF loses its final CRC byte on playback.
       Padding is outside the MMMD length and leaves all SMAF bytes intact.
       Bitmap records have the same filesystem read constraint. */
    if(im->size&1u){
        uint8_t *padded=realloc(im->file,(size_t)im->size+1);
        if(!padded){free(im->file);im->file=NULL;return fail(im,"cannot pad the phone content buffer");}
        im->file=padded;im->file[im->size++]=0;
    }
    if(im->size>0x10000){free(im->file);im->file=NULL;return fail(im,"phone content exceeds 64 KiB");}
    im->trigger = m->import_trigger; im->alloc = m->import_alloc;
    im->search = m->import_search; im->write = m->import_write;
    derive_stem(path, im->stem);
    strcpy(im->name, im->stem);
    im->active = 1;
    return 1;
}
void content_import_release(ContentImport *im)
{
    free(im->file);free(im->jad); memset(im, 0, sizeof(*im));
}
int content_java_menu(ContentImport *im,Machine *m)
{
    memset(im,0,sizeof(*im));
    if(!m->java_ready)return fail(im,"model declares no Java menu entries");
    im->type=10;im->trigger=m->java_ready;im->active=1;strcpy(im->name,"Java games");return 1;
}
static int java_run(ContentImport *im,Machine *m)
{
    Arm7 *c=&m->cpu;static const char url[]="http://sghemu.invalid/package.jad";
    /* An RTOS task may visit the idle entry while an injected call is still
       suspended. Only our private return address proves that call returned. */
    const uint32_t returned=0xffff0000u;
    uint32_t g=m->java_globals;
    if(!im->stage&&c->instructions<m->java_min_instructions)return 1;
    if(c->r[15]!=(im->stage?returned:im->trigger))return 1;
    if(!(c->cpsr&ARM_T))return fail(im,"Java entry was not reached in Thumb mode");
    switch(im->stage){
    case 0:
        im->caller=*c;
        if(!im->file){c->r[0]=m->java_message;c->r[1]=0;c->r[14]=returned|1u;
            c->r[15]=m->java_send_ui;im->stage=5;return 1;}
        c->r[0]=im->size;im->stage=1;break;
    case 1:
        if(!c->r[0])return fail(im,"cannot allocate the guest JAR buffer");
        im->data=c->r[0];c->r[0]=(uint32_t)strlen(im->jad)+1;im->stage=2;break;
    case 2:
        if(!c->r[0])return fail(im,"cannot allocate the guest JAD buffer");
        im->jad_ptr=c->r[0];c->r[0]=sizeof(url);im->stage=3;break;
    case 3:
        if(!c->r[0])return fail(im,"cannot allocate the guest URL buffer");im->url_ptr=c->r[0];
        if(!poke(m,im->data,im->file,im->size)||
           !poke(m,im->jad_ptr,(uint8_t *)im->jad,(uint32_t)strlen(im->jad)+1)||
           !poke(m,im->url_ptr,(const uint8_t *)url,sizeof(url)))return fail(im,"Java buffers are not writable");
        /* DeleteInitJad frees both pointers. They MUST be independent heap blocks. */
        bus_write(&m->bus,g-4,4,im->url_ptr);bus_write(&m->bus,g,4,sizeof(url)-1);
        bus_write(&m->bus,g+4,4,im->jad_ptr);bus_write(&m->bus,g+8,4,(uint32_t)strlen(im->jad));
        c->r[0]=im->data;c->r[1]=im->size;c->r[14]=returned|1u;c->r[15]=m->java_set_jar;
        im->stage=4;return 1;
    case 4:
        c->r[0]=m->java_message;c->r[1]=0;c->r[14]=returned|1u;c->r[15]=m->java_send_ui;
        im->stage=5;return 1;
    default:restore_caller(im,m);im->done=1;return 2;
    }
    c->r[14]=returned|1u;c->r[15]=im->alloc;return 1;
}
static int run(ContentImport *im, Machine *m)
{
    Arm7 *c = &m->cpu;
    if (!im->active || im->done) return 1;
    if (im->failed) return 0;
    if(im->type==10)return java_run(im,m);
    if (c->r[15] != im->trigger) return 1;
    if (!(c->cpsr & ARM_T)) return fail(im, "the guest left Thumb at the import trigger");
    switch (im->stage) {
    case 0:
        im->caller = *c;
        im->stage = 1;
        /* One block holds the file, the staged name, the search result and the
           out-parameter. Stack arguments go on the guest's own task stack so the
           writer's frame can never grow back into this block. */
        c->r[0] = im->size + 0x80;
        c->r[14] = im->trigger | 1u;
        c->r[15] = im->alloc;
        return 1;
    case 1:
        if (!c->r[0]) return fail(im, "the guest allocator returned null");
        im->base     = c->r[0] & ~3u;
        im->data     = im->base;
        im->name_ptr = (im->base + im->size + 3u) & ~3u;
        im->scratch  = (im->name_ptr + 16u) & ~3u;
        im->out      = (im->scratch + 0x30u) & ~3u;
        if (!poke(m, im->data, im->file, im->size) || !name_into_guest(m, im))
            return fail(im, "the guest import buffer is not writable memory");
        im->attempt = 0; im->stage = 2;
        c->r[0] = im->name_ptr; c->r[1] = im->scratch;
        c->r[14] = im->trigger | 1u; c->r[15] = im->search;
        return 1;
    case 2:
        /* flash_SearchByName returns 0 when the name is already taken. */
        if (c->r[0] != 0) {
            uint32_t zero = 0;
            im->stack = (im->caller.r[13] - 16u) & ~7u;
            if (!poke(m, im->out, (const uint8_t *)&zero, 4) || !stack_args(m, im))
                return fail(im, "cannot stage the guest call arguments");
            c->r[0] = im->name_ptr; c->r[1] = im->type; c->r[2] = im->data; c->r[3] = im->size;
            c->r[13] = im->stack;
            c->r[14] = im->trigger | 1u; c->r[15] = im->write;
            im->stage = 3;
            return 1;
        }
        if (++im->attempt >= 100) return fail(im, "no free file name is available");
        make_name(im, im->attempt);
        if (!name_into_guest(m, im)) return fail(im, "the guest import buffer is not writable memory");
        c->r[0] = im->name_ptr; c->r[1] = im->scratch;
        c->r[14] = im->trigger | 1u; c->r[15] = im->search;
        return 1;
    default:
        /* The writer answers 6 when it queued the job (0 is its "already done"
           path); anything else means it refused before touching flash. */
        if (c->r[0] != 0 && c->r[0] != 6) return fail(im, "the guest writer rejected the file");
        restore_caller(im, m);
        im->done = 1;
        return 2;
    }
}
int content_import_step(ContentImport *im, Machine *m)
{
    int result = run(im, m);
    /* A rejected or aborted import leaves the guest mid-call; put its own state
       back so a normal boot can still finish and persist what it has. */
    if (!result && im->stage) restore_caller(im, m);
    m->guest_busy=im->stage&&!im->done&&!im->failed;
    return result;
}
