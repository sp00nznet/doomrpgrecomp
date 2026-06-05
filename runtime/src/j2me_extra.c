/*
 * j2me_extra.c -- small constructors/stubs for the high-level MIDP surface that
 * only the later ports touch (Orcs & Elves II's optional online "more games" /
 * registration UI): java.util.Date, high-level lcdui Command/Form, and the
 * generic-connection framework. We have no network or high-level UI, so Form/
 * Command are inert objects and Connector.open() reports "not found" -- which the
 * games catch to fall back to offline behaviour.
 */
#include "j2me/runtime.h"
#include "doomrpg.h"

void m_java_util_Date___init_____V(jref this_) { (void)this_; }

void m_javax_microedition_lcdui_Command___init___Ljava_lang_StringII__V(jref this_, jref label, jint type, jint pri) {
    (void)this_; (void)label; (void)type; (void)pri;
}
void m_javax_microedition_lcdui_Form___init___Ljava_lang_String__V(jref this_, jref title) {
    (void)this_; (void)title;
}

/* No networking on the desktop build: report the connection can't be opened so
 * the caller's catch handles the offline case. */
jref m_javax_microedition_io_Connector__open__Ljava_lang_String__Ljavax_microedition_io_Connection(jref url) {
    (void)url;
    j_throw_class(&CLASS_javax_microedition_io_ConnectionNotFoundException, "no network");
    return 0;
}
