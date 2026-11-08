// test_writeapi.cpp -- the pure half of qdwrite.h.
//
// WHY THESE ASSERTIONS AND NOT "IT COMPILES"
// ------------------------------------------
// The live write paths cannot be debugged without the game, and a wrong frame does not
// fail loudly there: ProcessEvent on a wrong-class or wrong-offset frame returns
// successfully and changes nothing (API-GAPS section 0, DollsMP F-12.17).  So the two
// things that CAN be proven off-target are proven here:
//
//   oracle   the measured {FName@0, bool@8, i32@0xC} frame accepts a matching property
//            list and rejects one that differs by offset, by type, or by order.  If
//            those rejections did not fire, a blueprint recompile would silently turn a
//            write into a no-op.
//   frame    a value lands at the offset the layout declares, never at 0 or at a
//            hardcoded literal, and a frame whose ParmsSize or parameter lies outside
//            the buffer is refused rather than written past.
//
// The layout used here is the measured one from API-GAPS section 1, so these tests
// encode the specification rather than a second copy of the implementation.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qdwrite.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                      \
    do {                                                                       \
        if (cond) { ++g_pass; printf("PASS %s\n", name); }                     \
        else { ++g_fail; printf("FAIL %s (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

using namespace qd;
using namespace qd::wr;

static void SetField(ParamLayout& l, int i, const char* name, PKind kind,
                     int32_t offset, int32_t size, uint64_t flags = 0)
{
    int k = 0;
    for (; name[k] && k < kParamName - 1; ++k) l.p[i].name[k] = name[k];
    l.p[i].name[k] = 0;
    l.p[i].kind = kind;
    l.p[i].offset = offset;
    l.p[i].size = size;
    l.p[i].flags = flags;
}

// The measured frame: {FName key @0, bool wasSet @8, int32 value @0xC}.
static ParamLayout Measured()
{
    ParamLayout l;
    memset(&l, 0, sizeof(l));
    l.n = 3;
    l.parms_size = 16;
    SetField(l, 0, "Key",    PK_NAME, 0,  8);
    SetField(l, 1, "WasSet", PK_BOOL, 8,  1);
    SetField(l, 2, "Value",  PK_INT,  12, 4);
    return l;
}

// The shape the live build actually reflects (LIVE-TEST-LOG Session 6): the Blueprint
// library world context is a real parameter of the UFunction, and this build declares
// Value before WasSet.  ParmsSize 21 is exactly 8+8+4+1, so the four fields tile the
// parameter block with no padding left over.
static ParamLayout LiveShape()
{
    ParamLayout l;
    memset(&l, 0, sizeof(l));
    l.n = 4;
    l.parms_size = 21;
    SetField(l, 0, "Name",           PK_NAME,   0,  8);
    SetField(l, 1, "__WorldContext", PK_OBJECT, 8,  8);
    SetField(l, 2, "Value",          PK_INT,    16, 4);
    SetField(l, 3, "WasSet",         PK_BOOL,   20, 1);
    return l;
}

// ===========================================================================
// oracle
// ===========================================================================
static void TestOracle()
{
    char err[192];

    {
        ParamLayout l = Measured();
        CHECK(MatchesNameDataOracle(l, err, sizeof(err)),
              "oracle/the measured frame is accepted");
    }

    // Offset: the bool moved by one byte.  A walker that read Offset_Internal from the
    // wrong FProperty slot produces exactly this shape.
    {
        ParamLayout l = Measured();
        l.p[1].offset = 9;
        l.parms_size = 16;
        CHECK(!MatchesNameDataOracle(l, err, sizeof(err)),
              "oracle/a one-byte offset change is rejected");
    }

    // Type: the bool slot is an int instead.  Same offsets, wrong field.
    {
        ParamLayout l = Measured();
        l.p[1].kind = PK_INT;
        l.p[1].size = 4;
        CHECK(!MatchesNameDataOracle(l, err, sizeof(err)),
              "oracle/a type change is rejected");
    }

    // Size: FName read as four bytes.  A candidate ElementSize of the wrong width.
    {
        ParamLayout l = Measured();
        l.p[0].size = 4;
        CHECK(!MatchesNameDataOracle(l, err, sizeof(err)),
              "oracle/a width change is rejected");
    }

    // Order: all three types, offsets and sizes are right, but the property chain lists
    // them in a different order.  A frame built in this order writes the value into the
    // key's slot, so it must not be accepted.
    {
        ParamLayout l = Measured();
        Field tmp = l.p[0];
        l.p[0] = l.p[1];
        l.p[1] = tmp;
        CHECK(!MatchesNameDataOracle(l, err, sizeof(err)),
              "oracle/a declaration-order change is rejected");
    }

    // A list with an extra parameter is not the measured frame either.
    {
        ParamLayout l = Measured();
        SetField(l, 3, "Extra", PK_BOOL, 14, 1);
        l.n = 4;
        CHECK(!MatchesNameDataOracle(l, err, sizeof(err)),
              "oracle/an extra parameter is rejected");
    }

    // The four-parameter shape this build reflects.  Measured live (Session 6) and
    // required: the world context is a real parameter, so refusing this shape refused
    // every write and blamed the offsets.
    {
        ParamLayout l = LiveShape();
        CHECK(MatchesNameDataOracle(l, err, sizeof(err)),
              "oracle/the four-parameter live shape is accepted");
    }

    // Same four fields, order changed: still refused.  Accepting a second SHAPE is not
    // accepting anything.
    {
        ParamLayout l = LiveShape();
        Field tmp = l.p[1];
        l.p[1] = l.p[2];
        l.p[2] = tmp;
        CHECK(!MatchesNameDataOracle(l, err, sizeof(err)),
              "oracle/a four-parameter order change is rejected");
    }

    // And a four-parameter list with the world context's width wrong: the FProperty walk
    // reading an object pointer as something narrower.
    {
        ParamLayout l = LiveShape();
        l.p[1].size = 4;
        CHECK(!MatchesNameDataOracle(l, err, sizeof(err)),
              "oracle/a four-parameter width change is rejected");
    }

    // Three and four are the only shapes; five is neither.
    {
        ParamLayout l = LiveShape();
        SetField(l, 4, "Fifth", PK_INT, 21, 4);
        l.n = 5;
        CHECK(!MatchesNameDataOracle(l, err, sizeof(err)),
              "oracle/five parameters is not a known frame");
    }
}

// ===========================================================================
// frame
// ===========================================================================
static void TestFrame()
{
    ParamLayout l = Measured();

    {
        Frame f;
        CHECK(FrameInit(f, l, (int)sizeof(f.bytes)), "frame/initialises from the layout");

        QdName n;
        n.comparison_index = 0x111;
        n.number = 2;
        CHECK(FramePutName(f, "Key", n), "frame/writes the FName");
        CHECK(FramePutBool(f, "WasSet", true), "frame/writes the bool");
        CHECK(FramePutInt(f, "Value", (int32_t)0x0BADF00D), "frame/writes the int");

        QdName got = *(const QdName*)(f.bytes + l.p[0].offset);
        CHECK(got.comparison_index == 0x111 && got.number == 2,
              "frame/the FName is at its declared offset");
        CHECK(f.bytes[8] == 1, "frame/the bool is at its declared offset");
        CHECK(*(const int32_t*)(f.bytes + 12) == (int32_t)0x0BADF00D,
              "frame/the int is at its declared offset");
        // Padding is zeroed by FrameInit, so an unwritten byte cannot look like data
        // from the previous call (the frame is reused across writes).
        CHECK(f.bytes[9] == 0 && f.bytes[10] == 0 && f.bytes[11] == 0,
              "frame/padding around the bool stays zero");
    }

    // ParmsSize larger than the buffer: the call would read past the frame.
    {
        Frame f;
        CHECK(!FrameInit(f, l, 8), "frame/refuses ParmsSize larger than the buffer");
    }

    // A parameter that ends past ParmsSize.  The layout itself is malformed, so nothing
    // may be written from it.
    {
        ParamLayout bad = Measured();
        bad.parms_size = 12;              // Value @12 size 4 now ends at 16 > 12
        Frame f;
        CHECK(!FrameInit(f, bad, (int)sizeof(f.bytes)),
              "frame/refuses a parameter outside ParmsSize");
    }

    // A value wider than its parameter: an int into the bool slot.  This is the
    // type/size mismatch a wrong FProperty tag would otherwise hide.
    {
        Frame f;
        CHECK(FrameInit(f, l, (int)sizeof(f.bytes)), "frame/initialises for the width test");
        CHECK(!FramePutInt(f, "WasSet", 7), "frame/refuses an int into a bool slot");
        CHECK(!f.ok, "frame/a refused write poisons the frame");
    }

    // An unknown parameter name is a hard failure, never a silent skip.
    {
        Frame f;
        CHECK(FrameInit(f, l, (int)sizeof(f.bytes)), "frame/initialises for the name test");
        CHECK(!FramePutInt(f, "NotAParameter", 1),
              "frame/refuses an unknown parameter name");
    }

    // A put before init must not touch memory.
    {
        Frame f;
        memset(&f, 0, sizeof(f));
        uint8_t before[4] = {0xAA, 0xAA, 0xAA, 0xAA};
        memcpy(f.bytes, before, 4);
        CHECK(!FramePutInt(f, "Value", 1), "frame/refuses a write before init");
        CHECK(f.bytes[0] == 0xAA, "frame/a refused pre-init write changed nothing");
    }
}

// ===========================================================================
// type tags and the reflected bridge
// ===========================================================================
static void TestTypes()
{
    CHECK(KindOfType("NameProperty") == PK_NAME, "types/NameProperty");
    CHECK(KindOfType("BoolProperty") == PK_BOOL, "types/BoolProperty");
    CHECK(KindOfType("IntProperty") == PK_INT, "types/IntProperty");
    CHECK(KindOfType("StructProperty") == PK_STRUCT, "types/StructProperty");
    CHECK(KindOfType("SoftObjectProperty") == PK_SOFT, "types/SoftObjectProperty");
    CHECK(KindOfType("ClassProperty") == PK_OBJECT,
          "types/ClassProperty is object-shaped (a UClass* is a UObject*)");
    CHECK(KindOfType("VectorProperty") == PK_OTHER, "types/an unknown property is not guessed");

    // The live path feeds qdreflect's LayoutResult through FromReflected; the pure
    // tests above must see exactly what the walk produced, in the same order.
    LayoutResult r;
    memset(&r, 0, sizeof(r));
    r.valid = true;
    r.props = 3;
    r.parms_size = 16;
    for (int i = 0; i < 3; ++i) {
        const char* nm = (i == 0 ? "Key" : (i == 1 ? "WasSet" : "Value"));
        const char* ty = (i == 0 ? "NameProperty" : (i == 1 ? "BoolProperty" : "IntProperty"));
        int k = 0;
        for (; nm[k]; ++k) r.p[i].name[k] = nm[k];
        for (k = 0; ty[k]; ++k) r.p[i].type[k] = ty[k];
    }
    r.p[0].offset = 0;  r.p[0].size = 8;
    r.p[1].offset = 8;  r.p[1].size = 1;
    r.p[2].offset = 12; r.p[2].size = 4;
    r.p[2].flags = CPF_Parm;

    ParamLayout out;
    char err[192];
    CHECK(FromReflected(r, &out, err, sizeof(err)),
          "reflected/a valid signature converts");
    CHECK(out.n == 3, "reflected/the parameter count is preserved");
    CHECK(out.p[0].kind == PK_NAME && out.p[0].offset == 0 && out.p[0].size == 8,
          "reflected/the key keeps its type and offset");
    CHECK(out.p[1].kind == PK_BOOL && out.p[1].offset == 8 && out.p[1].size == 1,
          "reflected/the bool keeps its type and offset");
    CHECK(out.p[2].kind == PK_INT && out.p[2].offset == 12 && out.p[2].size == 4,
          "reflected/the value keeps its type and offset");
    CHECK(MatchesNameDataOracle(out, err, sizeof(err)),
          "reflected/the converted measured frame passes the oracle");

    // An invalid signature must not be convertible: an unproven walk is not a layout.
    {
        LayoutResult bad;
        memset(&bad, 0, sizeof(bad));
        bad.valid = false;
        ParamLayout o2;
        CHECK(!FromReflected(bad, &o2, err, sizeof(err)),
              "reflected/an invalid signature is refused");
    }
}

// ===========================================================================
// the soft-object record (G3)
// ===========================================================================
// LIVE-TEST-LOG Session 6: `AddItemToInventory` was called with a correct path, a
// correct count and a correct __WorldContext, returned success, and the native counter
// read `0 -> 0`.  The cause was the soft record's internal layout: the FName was written
// at +0 instead of +0x10, so the path the engine read out of the parameter was 16 bytes
// of the sub-path length and tag instead of the asset name.
//
// These assertions encode the measured layout (FWeakObjectPtr 8, TagAtLastTest+pad 8,
// FSoftObjectPath{FName, FString}) so a future edit cannot quietly move it again, and
// pin that the constants themselves are the ones the two already-correct call sites in
// this tree use (`avatar::LoadAsset`, `DevGive`).
static void TestSoftRecord()
{
    // Read through `volatile` so these are real comparisons at run time rather than
    // compile-time constants: /W4 reports a constant condition inside `if` as C4127, and
    // this project treats a suppressed warning as a numeric mistake waiting to happen.
    volatile int weakOff     = kSoftWeakOff;
    volatile int pathNameOff = kSoftPathNameOff;
    volatile int subPathOff  = kSoftSubPathOff;
    volatile int minSize     = kSoftMinSize;

    const int wo = weakOff, pno = pathNameOff, spo = subPathOff, ms = minSize;

    CHECK(pno == 0x10,
          "soft/the FSoftObjectPath starts at +0x10, after the weak pointer and tag");
    CHECK(spo == 0x18,
          "soft/the sub-path FString follows the asset FName");
    CHECK(wo == 0x00, "soft/the weak pointer is the first field");
    CHECK(ms == 0x28, "soft/the record is 40 bytes, the size this build reflects");

    // The offsets must tile a 40-byte record exactly: FString is { Data*, Num, Max } =
    // 8 + 4 + 4, so the sub-path ends where the record ends.
    CHECK(spo + 16 == ms,
          "soft/the FString ends exactly at the end of the record");
    // ...and the path name must sit entirely below the sub-path, with no overlap.
    CHECK(pno + 8 <= spo,
          "soft/the asset FName does not overrun the sub-path FString");

    // The AddItemToInventory signature this build reflects, as the live log measured it:
    // {Item : SoftObjectProperty @0 size 40, Count : IntProperty @40 size 4,
    //  __WorldContext : ObjectProperty @48 size 8}, ParmsSize 56.
    ParamLayout l;
    memset(&l, 0, sizeof(l));
    l.n = 3;
    l.parms_size = 56;
    SetField(l, 0, "Item",           PK_SOFT,   0,  40);
    SetField(l, 1, "Count",          PK_INT,    40, 4);
    SetField(l, 2, "__WorldContext", PK_OBJECT, 48, 8);

    // The world context is found by NAME, so it can never be mistaken for the item even
    // though both are pointers, and the count is found by name and must not be the item.
    const Field* ctx = WorldContextInput(l);
    CHECK(ctx && NameEquals(ctx->name, "__WorldContext"),
          "soft/the world context is chosen by name, not by position");
    const Field* count = InputNamed(l, PK_INT, "Count");
    CHECK(count && count->offset == 40, "soft/the count is found by name at its offset");

    const Field* item = ItemParam(l, ctx, count);
    CHECK(item && NameEquals(item->name, "Item"),
          "soft/the item pin is the soft reference and not the world context");
    CHECK(item && item->kind == PK_SOFT && item->size == 40,
          "soft/the item pin keeps the reflected 40-byte size");

    // A build that declares the item as a real object pointer must select THAT pin: the
    // record form would put a weak pointer where an object pointer is read.
    {
        ParamLayout o;
        memset(&o, 0, sizeof(o));
        o.n = 3;
        o.parms_size = 20;
        SetField(o, 0, "Item",           PK_OBJECT, 0,  8);
        SetField(o, 1, "Count",          PK_INT,    8,  4);
        SetField(o, 2, "__WorldContext", PK_OBJECT, 16, 8);
        const Field* octx = WorldContextInput(o);
        const Field* ocnt = InputNamed(o, PK_INT, "Count");
        const Field* oitem = ItemParam(o, octx, ocnt);
        CHECK(oitem && oitem->kind == PK_OBJECT && oitem->offset == 0,
              "soft/an object-typed item pin wins over the soft form");
        CHECK(octx && octx->offset == 16,
              "soft/the object item is not mistaken for the world context");
    }

    // TryConsumingItemFromInventory carries the same Item pin plus a Success bool.  The
    // bool is an OUT parameter and must never be chosen as the item or the count.
    {
        ParamLayout c;
        memset(&c, 0, sizeof(c));
        c.n = 4;
        c.parms_size = 57;
        SetField(c, 0, "Item",           PK_SOFT,   0,  40);
        SetField(c, 1, "Count",          PK_INT,    40, 4);
        SetField(c, 2, "__WorldContext", PK_OBJECT, 48, 8);
        SetField(c, 3, "Success",        PK_BOOL,   56, 1, CPF_OutParm);
        const Field* cctx = WorldContextInput(c);
        const Field* ccnt = InputNamed(c, PK_INT, "Count");
        const Field* citem = ItemParam(c, cctx, ccnt);
        CHECK(citem && NameEquals(citem->name, "Item"),
              "soft/the consumer's item pin is the soft reference");
        CHECK(ccnt && ccnt->offset == 40, "soft/the consumer's count offset matches");
        CHECK(cctx && cctx->offset == 48, "soft/the consumer's world context offset matches");
    }
}

int main()
{
    printf("=== qd write-api tests ===\n");
    TestOracle();
    TestFrame();
    TestTypes();
    TestSoftRecord();
    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
