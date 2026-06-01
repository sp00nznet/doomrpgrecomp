/*
 * class_meta.c -- the jclass metadata + virtual-method tables for every runtime
 * class the game touches, plus array-class singletons and the two runtime
 * statics (System.out, AlertType.ERROR). Tables list only the methods reached
 * by invokevirtual/invokeinterface (j_vfind); static/<init> calls bind directly.
 *
 * Exception classes carry a real super chain so the translator's catch dispatch
 * (which uses j_instanceof against the catch type) matches Java semantics.
 */
#include "j2me/runtime.h"
#include "doomrpg.h"

/* Object.equals/toString are referenced here but not declared in doomrpg.h */
jint m_java_lang_Object__equals__Ljava_lang_Object__Z(jref, jref);
jref m_java_lang_Object__toString____Ljava_lang_String(jref);

#define M(n, d, f) { n, d, (void *)&f }
#define CLASS(sym, nm, sup, sz, tbl) \
    const jclass sym = { nm, sup, (uint32_t)(sz), tbl, \
        (uint32_t)(sizeof(tbl) / sizeof((tbl)[0])), 0, 0, 0, 0 }
#define CLASS0(sym, nm, sup, sz) \
    const jclass sym = { nm, sup, (uint32_t)(sz), 0, 0, 0, 0, 0, 0 }

/* ===== java.lang.Object ==================================================== */
static const jmethod T_Object[] = {
    M("getClass", "()Ljava/lang/Class;", m_java_lang_Object__getClass____Ljava_lang_Class),
    M("equals", "(Ljava/lang/Object;)Z", m_java_lang_Object__equals__Ljava_lang_Object__Z),
    M("toString", "()Ljava/lang/String;", m_java_lang_Object__toString____Ljava_lang_String),
};
CLASS(CLASS_java_lang_Object, "java/lang/Object", 0, sizeof(jobject), T_Object);

/* ===== java.lang.Class ===================================================== */
static const jmethod T_Class[] = {
    M("getResourceAsStream", "(Ljava/lang/String;)Ljava/io/InputStream;",
      m_java_lang_Class__getResourceAsStream__Ljava_lang_String__Ljava_io_InputStream),
};
CLASS(CLASS_java_lang_Class, "java/lang/Class", &CLASS_java_lang_Object, sizeof(ClassObj), T_Class);

/* ===== java.lang.String ==================================================== */
static const jmethod T_String[] = {
    M("charAt", "(I)C", m_java_lang_String__charAt__I__C),
    M("length", "()I", m_java_lang_String__length____I),
    M("equals", "(Ljava/lang/Object;)Z", m_java_lang_String__equals__Ljava_lang_Object__Z),
    M("compareTo", "(Ljava/lang/String;)I", m_java_lang_String__compareTo__Ljava_lang_String__I),
    M("indexOf", "(II)I", m_java_lang_String__indexOf__II__I),
    M("lastIndexOf", "(I)I", m_java_lang_String__lastIndexOf__I__I),
    M("startsWith", "(Ljava/lang/String;)Z", m_java_lang_String__startsWith__Ljava_lang_String__Z),
    M("substring", "(I)Ljava/lang/String;", m_java_lang_String__substring__I__Ljava_lang_String),
    M("substring", "(II)Ljava/lang/String;", m_java_lang_String__substring__II__Ljava_lang_String),
    M("replace", "(CC)Ljava/lang/String;", m_java_lang_String__replace__CC__Ljava_lang_String),
};
CLASS(CLASS_java_lang_String, "java/lang/String", &CLASS_java_lang_Object, sizeof(StringObj), T_String);

/* ===== java.lang.StringBuffer ============================================== */
static const jmethod T_StringBuffer[] = {
    M("append", "(Ljava/lang/String;)Ljava/lang/StringBuffer;", m_java_lang_StringBuffer__append__Ljava_lang_String__Ljava_lang_StringBuffer),
    M("append", "(Ljava/lang/Object;)Ljava/lang/StringBuffer;", m_java_lang_StringBuffer__append__Ljava_lang_Object__Ljava_lang_StringBuffer),
    M("append", "(C)Ljava/lang/StringBuffer;", m_java_lang_StringBuffer__append__C__Ljava_lang_StringBuffer),
    M("append", "(I)Ljava/lang/StringBuffer;", m_java_lang_StringBuffer__append__I__Ljava_lang_StringBuffer),
    M("append", "(J)Ljava/lang/StringBuffer;", m_java_lang_StringBuffer__append__J__Ljava_lang_StringBuffer),
    M("charAt", "(I)C", m_java_lang_StringBuffer__charAt__I__C),
    M("length", "()I", m_java_lang_StringBuffer__length____I),
    M("setCharAt", "(IC)V", m_java_lang_StringBuffer__setCharAt__IC__V),
    M("setLength", "(I)V", m_java_lang_StringBuffer__setLength__I__V),
    M("toString", "()Ljava/lang/String;", m_java_lang_StringBuffer__toString____Ljava_lang_String),
    M("insert", "(IC)Ljava/lang/StringBuffer;", m_java_lang_StringBuffer__insert__IC__Ljava_lang_StringBuffer),
    M("insert", "(ILjava/lang/String;)Ljava/lang/StringBuffer;", m_java_lang_StringBuffer__insert__ILjava_lang_String__Ljava_lang_StringBuffer),
    M("insert", "(ILjava/lang/Object;)Ljava/lang/StringBuffer;", m_java_lang_StringBuffer__insert__ILjava_lang_Object__Ljava_lang_StringBuffer),
};
CLASS(CLASS_java_lang_StringBuffer, "java/lang/StringBuffer", &CLASS_java_lang_Object, sizeof(StringBufferObj), T_StringBuffer);

/* ===== boxed / misc java.lang ============================================== */
CLASS0(CLASS_java_lang_Integer, "java/lang/Integer", &CLASS_java_lang_Object, sizeof(IntegerObj));

static const jmethod T_Runtime[] = {
    M("freeMemory", "()J", m_java_lang_Runtime__freeMemory____J),
    M("totalMemory", "()J", m_java_lang_Runtime__totalMemory____J),
};
CLASS(CLASS_java_lang_Runtime, "java/lang/Runtime", &CLASS_java_lang_Object, sizeof(jobject), T_Runtime);

static const jmethod T_Thread[] = {
    M("start", "()V", m_java_lang_Thread__start____V),
};
CLASS(CLASS_java_lang_Thread, "java/lang/Thread", &CLASS_java_lang_Object, sizeof(ThreadObj), T_Thread);

static const jmethod T_Random[] = {
    M("nextInt", "()I", m_java_util_Random__nextInt____I),
};
CLASS(CLASS_java_util_Random, "java/util/Random", &CLASS_java_lang_Object, sizeof(RandomObj), T_Random);

static const jmethod T_Vector[] = {
    M("addElement", "(Ljava/lang/Object;)V", m_java_util_Vector__addElement__Ljava_lang_Object__V),
    M("elementAt", "(I)Ljava/lang/Object;", m_java_util_Vector__elementAt__I__Ljava_lang_Object),
    M("size", "()I", m_java_util_Vector__size____I),
};
CLASS(CLASS_java_util_Vector, "java/util/Vector", &CLASS_java_lang_Object, sizeof(VectorObj), T_Vector);

/* ===== Throwable hierarchy ================================================= */
static const jmethod T_Throwable[] = {
    M("toString", "()Ljava/lang/String;", m_java_lang_Throwable__toString____Ljava_lang_String),
};
CLASS(CLASS_java_lang_Exception, "java/lang/Exception", &CLASS_java_lang_Object, sizeof(ThrowableObj), T_Throwable);
CLASS0(CLASS_java_lang_RuntimeException, "java/lang/RuntimeException", &CLASS_java_lang_Exception, sizeof(ThrowableObj));
CLASS0(CLASS_java_lang_IllegalArgumentException, "java/lang/IllegalArgumentException", &CLASS_java_lang_RuntimeException, sizeof(ThrowableObj));
CLASS0(CLASS_java_lang_InterruptedException, "java/lang/InterruptedException", &CLASS_java_lang_Exception, sizeof(ThrowableObj));
CLASS0(CLASS_java_io_IOException, "java/io/IOException", &CLASS_java_lang_Exception, sizeof(ThrowableObj));
CLASS(CLASS_java_lang_Error, "java/lang/Error", &CLASS_java_lang_Object, sizeof(ThrowableObj), T_Throwable);

/* ===== java.io streams ===================================================== */
static const jmethod T_BAIS[] = {
    M("read", "()I", m_java_io_InputStream__read____I),
    M("read", "([BII)I", m_java_io_InputStream__read__aBII__I),
    M("skip", "(J)J", m_java_io_InputStream__skip__J__J),
    M("close", "()V", m_java_io_InputStream__close____V),
};
CLASS(CLASS_java_io_ByteArrayInputStream, "java/io/ByteArrayInputStream", &CLASS_java_lang_Object, sizeof(StreamObj), T_BAIS);

static const jmethod T_BAOS[] = {
    M("toByteArray", "()[B", m_java_io_ByteArrayOutputStream__toByteArray____aB),
};
CLASS(CLASS_java_io_ByteArrayOutputStream, "java/io/ByteArrayOutputStream", &CLASS_java_lang_Object, sizeof(StreamObj), T_BAOS);

static const jmethod T_DIS[] = {
    M("read", "()I", m_java_io_DataInputStream__read____I),
    M("readByte", "()B", m_java_io_DataInputStream__readByte____B),
    M("readBoolean", "()Z", m_java_io_DataInputStream__readBoolean____Z),
    M("readShort", "()S", m_java_io_DataInputStream__readShort____S),
    M("readInt", "()I", m_java_io_DataInputStream__readInt____I),
    M("readLong", "()J", m_java_io_DataInputStream__readLong____J),
    M("readUTF", "()Ljava/lang/String;", m_java_io_DataInputStream__readUTF____Ljava_lang_String),
};
CLASS(CLASS_java_io_DataInputStream, "java/io/DataInputStream", &CLASS_java_lang_Object, sizeof(StreamObj), T_DIS);

static const jmethod T_DOS[] = {
    M("writeBoolean", "(Z)V", m_java_io_DataOutputStream__writeBoolean__Z__V),
    M("writeByte", "(I)V", m_java_io_DataOutputStream__writeByte__I__V),
    M("writeShort", "(I)V", m_java_io_DataOutputStream__writeShort__I__V),
    M("writeInt", "(I)V", m_java_io_DataOutputStream__writeInt__I__V),
    M("writeLong", "(J)V", m_java_io_DataOutputStream__writeLong__J__V),
    M("writeUTF", "(Ljava/lang/String;)V", m_java_io_DataOutputStream__writeUTF__Ljava_lang_String__V),
};
CLASS(CLASS_java_io_DataOutputStream, "java/io/DataOutputStream", &CLASS_java_lang_Object, sizeof(StreamObj), T_DOS);

static const jmethod T_PrintStream[] = {
    M("println", "(Ljava/lang/String;)V", m_java_io_PrintStream__println__Ljava_lang_String__V),
};
CLASS(CLASS_java_io_PrintStream, "java/io/PrintStream", &CLASS_java_lang_Object, sizeof(jobject), T_PrintStream);

/* ===== lcdui =============================================================== */
static const jmethod T_Graphics[] = {
    M("setColor", "(I)V", m_javax_microedition_lcdui_Graphics__setColor__I__V),
    M("fillRect", "(IIII)V", m_javax_microedition_lcdui_Graphics__fillRect__IIII__V),
    M("drawRect", "(IIII)V", m_javax_microedition_lcdui_Graphics__drawRect__IIII__V),
    M("drawLine", "(IIII)V", m_javax_microedition_lcdui_Graphics__drawLine__IIII__V),
    M("translate", "(II)V", m_javax_microedition_lcdui_Graphics__translate__II__V),
    M("getTranslateX", "()I", m_javax_microedition_lcdui_Graphics__getTranslateX____I),
    M("getTranslateY", "()I", m_javax_microedition_lcdui_Graphics__getTranslateY____I),
    M("setClip", "(IIII)V", m_javax_microedition_lcdui_Graphics__setClip__IIII__V),
    M("getClipX", "()I", m_javax_microedition_lcdui_Graphics__getClipX____I),
    M("getClipY", "()I", m_javax_microedition_lcdui_Graphics__getClipY____I),
    M("getClipWidth", "()I", m_javax_microedition_lcdui_Graphics__getClipWidth____I),
    M("getClipHeight", "()I", m_javax_microedition_lcdui_Graphics__getClipHeight____I),
    M("drawImage", "(Ljavax/microedition/lcdui/Image;III)V", m_javax_microedition_lcdui_Graphics__drawImage__Ljavax_microedition_lcdui_ImageIII__V),
    M("drawRegion", "(Ljavax/microedition/lcdui/Image;IIIIIIII)V", m_javax_microedition_lcdui_Graphics__drawRegion__Ljavax_microedition_lcdui_ImageIIIIIIII__V),
    M("drawRGB", "([IIIIIIIZ)V", m_javax_microedition_lcdui_Graphics__drawRGB__aIIIIIIIZ__V),
};
CLASS(CLASS_javax_microedition_lcdui_Graphics, "javax/microedition/lcdui/Graphics", &CLASS_java_lang_Object, sizeof(GraphicsObj), T_Graphics);

CLASS0(CLASS_javax_microedition_lcdui_Image, "javax/microedition/lcdui/Image", &CLASS_java_lang_Object, sizeof(ImageObj));

/* GameCanvas flattens the Canvas methods the game calls on its subclass `k`. */
static const jmethod T_GameCanvas[] = {
    M("getGraphics", "()Ljavax/microedition/lcdui/Graphics;", m_javax_microedition_lcdui_game_GameCanvas__getGraphics____Ljavax_microedition_lcdui_Graphics),
    M("flushGraphics", "()V", m_javax_microedition_lcdui_game_GameCanvas__flushGraphics____V),
    M("setFullScreenMode", "(Z)V", m_javax_microedition_lcdui_Canvas__setFullScreenMode__Z__V),
    M("getGameAction", "(I)I", m_javax_microedition_lcdui_Canvas__getGameAction__I__I),
};
CLASS(CLASS_javax_microedition_lcdui_game_GameCanvas, "javax/microedition/lcdui/game/GameCanvas", &CLASS_java_lang_Object, sizeof(GameCanvasObj), T_GameCanvas);

static const jmethod T_Display[] = {
    M("setCurrent", "(Ljavax/microedition/lcdui/Displayable;)V", m_javax_microedition_lcdui_Display__setCurrent__Ljavax_microedition_lcdui_Displayable__V),
    M("vibrate", "(I)Z", m_javax_microedition_lcdui_Display__vibrate__I__Z),
};
CLASS(CLASS_javax_microedition_lcdui_Display, "javax/microedition/lcdui/Display", &CLASS_java_lang_Object, sizeof(jobject), T_Display);

static const jmethod T_Alert[] = {
    M("setString", "(Ljava/lang/String;)V", m_javax_microedition_lcdui_Alert__setString__Ljava_lang_String__V),
    M("setTimeout", "(I)V", m_javax_microedition_lcdui_Alert__setTimeout__I__V),
    M("setType", "(Ljavax/microedition/lcdui/AlertType;)V", m_javax_microedition_lcdui_Alert__setType__Ljavax_microedition_lcdui_AlertType__V),
};
CLASS(CLASS_javax_microedition_lcdui_Alert, "javax/microedition/lcdui/Alert", &CLASS_java_lang_Object, sizeof(jobject), T_Alert);

/* ===== media (Player == VolumeControl) ===================================== */
static const jmethod T_Player[] = {
    M("realize", "()V", m_javax_microedition_media_Player__realize____V),
    M("start", "()V", m_javax_microedition_media_Player__start____V),
    M("close", "()V", m_javax_microedition_media_Player__close____V),
    M("getState", "()I", m_javax_microedition_media_Player__getState____I),
    M("setLoopCount", "(I)V", m_javax_microedition_media_Player__setLoopCount__I__V),
    M("getControl", "(Ljava/lang/String;)Ljavax/microedition/media/Control;", m_javax_microedition_media_Controllable__getControl__Ljava_lang_String__Ljavax_microedition_media_Control),
    M("setLevel", "(I)I", m_javax_microedition_media_control_VolumeControl__setLevel__I__I),
};
CLASS(CLASS_javax_microedition_media_control_VolumeControl, "javax/microedition/media/control/VolumeControl", &CLASS_java_lang_Object, sizeof(PlayerObj), T_Player);

/* ===== rms ================================================================= */
static const jmethod T_RecordStore[] = {
    M("addRecord", "([BII)I", m_javax_microedition_rms_RecordStore__addRecord__aBII__I),
    M("getRecord", "(I)[B", m_javax_microedition_rms_RecordStore__getRecord__I__aB),
    M("setRecord", "(I[BII)V", m_javax_microedition_rms_RecordStore__setRecord__IaBII__V),
    M("getNumRecords", "()I", m_javax_microedition_rms_RecordStore__getNumRecords____I),
    M("closeRecordStore", "()V", m_javax_microedition_rms_RecordStore__closeRecordStore____V),
    M("enumerateRecords", "(Ljavax/microedition/rms/RecordFilter;Ljavax/microedition/rms/RecordComparator;Z)Ljavax/microedition/rms/RecordEnumeration;", m_javax_microedition_rms_RecordStore__enumerateRecords__Ljavax_microedition_rms_RecordFilterLjavax_microedition_rms_RecordComparatorZ__Ljavax_microedition_rms_RecordEnumeration),
};
CLASS(CLASS_javax_microedition_rms_RecordStore, "javax/microedition/rms/RecordStore", &CLASS_java_lang_Object, sizeof(RecordStoreObj), T_RecordStore);

static const jmethod T_RecordEnum[] = {
    M("nextRecordId", "()I", m_javax_microedition_rms_RecordEnumeration__nextRecordId____I),
};
CLASS(CLASS_javax_microedition_rms_RecordEnumeration, "javax/microedition/rms/RecordEnumeration", &CLASS_java_lang_Object, sizeof(EnumObj), T_RecordEnum);

static const jmethod T_MIDlet[] = {
    M("getAppProperty", "(Ljava/lang/String;)Ljava/lang/String;", m_javax_microedition_midlet_MIDlet__getAppProperty__Ljava_lang_String__Ljava_lang_String),
    M("notifyDestroyed", "()V", m_javax_microedition_midlet_MIDlet__notifyDestroyed____V),
};
CLASS(CLASS_javax_microedition_midlet_MIDlet, "javax/microedition/midlet/MIDlet", &CLASS_java_lang_Object, sizeof(MIDletObj), T_MIDlet);

/* ===== array classes ======================================================= */
#define ARRCLASS(sym, nm, at) \
    const jclass sym = { nm, &CLASS_java_lang_Object, 0, 0, 0, 0, 0, 0, (at) }
ARRCLASS(CLASS_array_boolean, "[Z", J_AT_Z);
ARRCLASS(CLASS_array_byte,    "[B", J_AT_B);
ARRCLASS(CLASS_array_char,    "[C", J_AT_C);
ARRCLASS(CLASS_array_short,   "[S", J_AT_S);
ARRCLASS(CLASS_array_int,     "[I", J_AT_I);
ARRCLASS(CLASS_array_long,    "[J", J_AT_J);
ARRCLASS(CLASS_array_float,   "[F", J_AT_F);
ARRCLASS(CLASS_array_double,  "[D", J_AT_D);
ARRCLASS(CLASS_array_ref,     "[L", J_AT_REF);
/* named array classes referenced by the bytecode (checkcast/anewarray) */
ARRCLASS(CLASS__u5bI,     "[I",  J_AT_I);
ARRCLASS(CLASS__u5b_u5bC, "[[C", J_AT_REF);

/* ===== runtime statics ===================================================== */
jref S_java_lang_System__out__Ljava_io_PrintStream;
jref S_javax_microedition_lcdui_AlertType__ERROR__Ljavax_microedition_lcdui_AlertType;

void runtime_init_statics(void) {
    S_java_lang_System__out__Ljava_io_PrintStream = j_new(&CLASS_java_io_PrintStream);
    S_javax_microedition_lcdui_AlertType__ERROR__Ljavax_microedition_lcdui_AlertType =
        j_new(&CLASS_java_lang_Object);
}
