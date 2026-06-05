/*
 * exceptions_extra.c -- (String)-message constructors for the exception types
 * the sibling games throw that the original Doom RPG didn't (IOException and the
 * RMS exception family). They all just stash the message on the ThrowableObj,
 * matching m_java_lang_Exception___init___Ljava_lang_String__V.
 */
#include "j2me/runtime.h"

static void set_msg(jref this_, jref msg) { ((ThrowableObj *)this_)->message = msg; }

void m_java_io_IOException___init___Ljava_lang_String__V(jref t, jref m) { set_msg(t, m); }

void m_javax_microedition_rms_RecordStoreException___init___Ljava_lang_String__V(jref t, jref m) { set_msg(t, m); }
void m_javax_microedition_rms_InvalidRecordIDException___init___Ljava_lang_String__V(jref t, jref m) { set_msg(t, m); }
void m_javax_microedition_rms_RecordStoreFullException___init___Ljava_lang_String__V(jref t, jref m) { set_msg(t, m); }
void m_javax_microedition_rms_RecordStoreNotFoundException___init___Ljava_lang_String__V(jref t, jref m) { set_msg(t, m); }
