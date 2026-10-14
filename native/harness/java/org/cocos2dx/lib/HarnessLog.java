package org.cocos2dx.lib;

/** PC harness only: records calls the original native code makes into Java. */
public final class HarnessLog {
    public static void call(String what) { System.err.println("[JAVA] " + what); }
}
