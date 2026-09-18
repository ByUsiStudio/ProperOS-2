// 文件名: codecin.java
// 包名: miao.byusi.libs.codecin
// Java 版本: 17
// 用途: iApp 裕语言 V3 应用内调用 codecin 原生库 (极简接口)

package miao.byusi.libs.codecin;

import java.io.*;
import java.nio.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

public class codecin {

    // ==================== 原生方法 ====================

    private static native long codecin_run(
            byte[] bytecode, int bcLen,
            byte[] mem, int memLen,
            long entry, long sp, long heapBase,
            byte[] input, int inputLen,
            long maxSteps);

    private static native void codecin_free(long ptr);

    private static native long codecin_crom_pack(
            byte[] mem, int memLen, int compress, int[] outLen);

    private static native long codecin_crom_unpack(
            byte[] data, int dataLen, int[] memLen, int[] flags);

    private static native String codecin_version();

    // 辅助: 从 native 内存读取字节 (需 Go 库导出)
    private static native byte[] codecin_read(long addr, int len);

    // ==================== 原生库加载 ====================

    private static boolean sLoaded = false;
    private static String sLoadError = null;
    private static String sLibPath = null;

    static {
        loadNative();
    }

    private static synchronized void loadNative() {
        if (sLoaded) return;

        // iApp 应用内常见路径
        String[] candidates = new String[] {
            // 应用私有目录 (推荐)
            "/data/data/com.iapp.app/files/libcodecin_native.so",
            "/data/data/com.iapp.app/files/codecin_native.so",
            // SD 卡 iApp 目录
            "/sdcard/iApp/libcodecin_native.so",
            "/sdcard/iApp/codecin_native.so",
            // SD 卡根目录
            "/sdcard/libcodecin_native.so",
            "/sdcard/codecin_native.so",
        };

        for (String path : candidates) {
            try {
                File f = new File(path);
                if (!f.exists()) continue;
                System.load(path);
                sLoaded = true;
                sLibPath = path;
                return;
            } catch (Throwable e) {
                sLoadError = e.toString();
            }
        }

        // 最后尝试系统库路径
        try {
            System.loadLibrary("codecin_native");
            sLoaded = true;
            sLibPath = "java.library.path";
        } catch (Throwable e) {
            sLoadError = e.toString();
            sLoaded = false;
        }
    }

    public static boolean isLoaded() { return sLoaded; }
    public static String getLoadError() { return sLoadError; }
    public static String getLibPath() { return sLibPath; }

    public static String version() {
        if (!sLoaded) return "unknown";
        try { return codecin_version(); } catch (Throwable e) { return "unknown"; }
    }

    // ==================== 常量 ====================

    public static final int BC_VERSION = 1;
    public static final int KIND_REG     = 0;
    public static final int KIND_IMM     = 1;
    public static final int KIND_VEC     = 2;
    public static final int KIND_VECLANE = 3;
    public static final int KIND_MEM     = 4;
    public static final int KIND_COND    = 5;
    public static final int KIND_FLOAT   = 6;
    public static final int KIND_STR     = 7;

    private static final long DEFAULT_MAX_STEPS = 100_000_000L;
    private static final int  DEFAULT_MEM_SIZE  = 1 << 20; // 1MB

    // ==================== 极简接口 ====================

    /**
     * 应用内执行 codecin 程序。
     *
     * @param execDir  执行目录 (存放执行文件、内存镜像、输入数据等)
     * @param execFile 执行文件名 (相对于 execDir，或绝对路径)
     * @param logFile  日志文件路径 (绝对路径，追加写入)
     * @return true=执行成功, false=失败 (详情写入日志)
     */
    public static boolean exec(String execDir, String execFile, String logFile) {
        PrintWriter log = null;
        try {
            log = openLog(logFile);
            logLine(log, "=== codecin exec ===");
            logLine(log, "time: " + new Date());
            logLine(log, "execDir: " + execDir);
            logLine(log, "execFile: " + execFile);

            // 1. 检查原生库
            if (!sLoaded) {
                logLine(log, "[FAIL] native lib not loaded: " + sLoadError);
                return false;
            }
            logLine(log, "lib: " + sLibPath + " (" + version() + ")");

            // 2. 解析执行文件路径
            File f = new File(execFile);
            if (!f.isAbsolute()) {
                f = new File(execDir, execFile);
            }
            if (!f.exists()) {
                logLine(log, "[FAIL] exec file not found: " + f.getAbsolutePath());
                return false;
            }
            logLine(log, "resolved file: " + f.getAbsolutePath()
                    + " (" + f.length() + " bytes)");

            // 3. 读取字节码
            byte[] bytecode = readFile(f);
            if (bytecode == null || bytecode.length < 13) {
                logLine(log, "[FAIL] bad bytecode file");
                return false;
            }

            // 4. 校验魔数/版本
            if (bytecode[0] != 'U' || bytecode[1] != 'C'
                    || bytecode[2] != 'B' || bytecode[3] != 'C') {
                logLine(log, "[FAIL] bad bytecode magic");
                return false;
            }
            int ver = bytecode[4] & 0xFF;
            if (ver != BC_VERSION) {
                logLine(log, "[FAIL] unsupported bytecode version: " + ver);
                return false;
            }

            // 读取 entry
            ByteBuffer bb = ByteBuffer.wrap(bytecode).order(ByteOrder.LITTLE_ENDIAN);
            bb.position(5);
            int entry = bb.getInt();
            logLine(log, "entry: " + entry);

            // 5. 准备内存镜像
            byte[] mem = loadMem(execDir, log);

            // 6. 准备输入数据 (可选)
            byte[] input = loadInput(execDir, log);

            // 7. 运行
            logLine(log, "running...");
            RunResult r = run(bytecode, mem, entry, 0, 0, input, DEFAULT_MAX_STEPS);
            if (r == null) {
                logLine(log, "[FAIL] codecin_run returned null");
                return false;
            }

            // 8. 写结果
            logLine(log, "status: " + r.status
                    + " halted=" + r.halted
                    + " pc=" + r.pc
                    + " sp=" + r.sp
                    + " heapPtr=" + r.heapPtr
                    + " steps=" + r.steps);

            if (r.output != null && !r.output.isEmpty()) {
                logLine(log, "--- output ---");
                logLine(log, r.output);
                logLine(log, "--- end output ---");
            }

            if (r.error != null && !r.error.isEmpty()) {
                logLine(log, "[ERROR] " + r.error);
            }

            // 9. 回写内存镜像 (可选)
            if (r.mem != null && r.mem.length > 0) {
                saveMem(execDir, r.mem, log);
            }

            boolean ok = r.halted && (r.error == null || r.error.isEmpty());
            logLine(log, ok ? "[OK] exec done" : "[FAIL] exec finished with error");
            return ok;

        } catch (Throwable e) {
            if (log != null) {
                logLine(log, "[EXCEPTION] " + e);
                e.printStackTrace(log);
            }
            return false;
        } finally {
            if (log != null) {
                logLine(log, "=== end ===");
                log.flush();
                log.close();
            }
        }
    }

    // ==================== 辅助: 文件 ====================

    private static PrintWriter openLog(String logFile) {
        try {
            File f = new File(logFile);
            File parent = f.getParentFile();
            if (parent != null && !parent.exists()) {
                parent.mkdirs();
            }
            return new PrintWriter(new BufferedWriter(
                    new OutputStreamWriter(
                            new FileOutputStream(f, true), StandardCharsets.UTF_8)));
        } catch (Throwable e) {
            // 日志打不开就退化为标准输出
            return new PrintWriter(System.out);
        }
    }

    private static void logLine(PrintWriter log, String msg) {
        if (log != null) {
            log.println(msg);
            log.flush();
        }
        // 同时输出到系统日志，方便 iApp 调试
        try { System.out.println("[codecin] " + msg); } catch (Throwable ignored) {}
    }

    private static byte[] readFile(File f) {
        try (FileInputStream in = new FileInputStream(f);
             ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
            }
            return out.toByteArray();
        } catch (Throwable e) {
            return null;
        }
    }

    private static boolean writeFile(File f, byte[] data) {
        try {
            File parent = f.getParentFile();
            if (parent != null && !parent.exists()) parent.mkdirs();
            try (FileOutputStream out = new FileOutputStream(f)) {
                out.write(data);
                out.flush();
            }
            return true;
        } catch (Throwable e) {
            return false;
        }
    }

    private static byte[] loadMem(String execDir, PrintWriter log) {
        // 优先找 mem.bin
        File memFile = new File(execDir, "mem.bin");
        if (memFile.exists()) {
            byte[] m = readFile(memFile);
            if (m != null && m.length > 0) {
                logLine(log, "mem loaded: " + m.length + " bytes from mem.bin");
                return m;
            }
        }
        // 其次找 crom 压缩包
        File cromFile = new File(execDir, "mem.crom");
        if (cromFile.exists()) {
            byte[] c = readFile(cromFile);
            if (c != null && c.length > 0) {
                byte[] m = cromUnpack(c);
                if (m != null) {
                    logLine(log, "mem unpacked from mem.crom: " + m.length + " bytes");
                    return m;
                } else {
                    logLine(log, "[WARN] crom unpack failed, use default mem");
                }
            }
        }
        logLine(log, "mem default: " + DEFAULT_MEM_SIZE + " bytes");
        return new byte[DEFAULT_MEM_SIZE];
    }

    private static void saveMem(String execDir, byte[] mem, PrintWriter log) {
        File memFile = new File(execDir, "mem.out.bin");
        if (writeFile(memFile, mem)) {
            logLine(log, "mem saved: " + mem.length + " bytes -> mem.out.bin");
        }
    }

    private static byte[] loadInput(String execDir, PrintWriter log) {
        File inFile = new File(execDir, "input.bin");
        if (inFile.exists()) {
            byte[] d = readFile(inFile);
            if (d != null) {
                logLine(log, "input loaded: " + d.length + " bytes");
                return d;
            }
        }
        return new byte[0];
    }

    // ==================== 运行结果 ====================

    public static class RunResult {
        public int status;
        public boolean halted;
        public long pc;
        public long sp;
        public long heapPtr;
        public long steps;
        public long[] regs = new long[33];
        public double[][] vecRegs = new double[32][4];
        public byte[] mem;
        public String output;
        public String error;

        @Override
        public String toString() {
            return "RunResult{status=" + status
                    + ", halted=" + halted
                    + ", pc=" + pc
                    + ", sp=" + sp
                    + ", heapPtr=" + heapPtr
                    + ", steps=" + steps
                    + ", output='" + output + '\''
                    + ", error='" + error + '\''
                    + '}';
        }
    }

    // ==================== 原生 VM ====================

    public static RunResult run(byte[] bytecode, byte[] mem, long entry,
                                long sp, long heapBase, byte[] inputData,
                                long maxSteps) {
        if (!sLoaded) return null;
        if (bytecode == null) return null;
        if (mem == null) mem = new byte[0];
        if (inputData == null) inputData = new byte[0];

        long ptr = 0;
        try {
            ptr = codecin_run(
                    bytecode, bytecode.length,
                    mem, mem.length,
                    entry, sp, heapBase,
                    inputData, inputData.length,
                    maxSteps);

            if (ptr == 0) return null;
            return parseResult(ptr);
        } catch (Throwable e) {
            sLoadError = e.toString();
            return null;
        } finally {
            if (ptr != 0) {
                try { codecin_free(ptr); } catch (Throwable ignored) {}
            }
        }
    }

    private static RunResult parseResult(long ptr) {
        RunResult r = new RunResult();

        byte[] head = nativeRead(ptr, 64);
        ByteBuffer hb = ByteBuffer.wrap(head).order(ByteOrder.LITTLE_ENDIAN);

        r.status = hb.get(0) & 0xFF;
        r.halted = (r.status == 0);

        hb.position(4);
        r.pc      = hb.getLong();
        r.sp      = hb.getLong();
        r.heapPtr = hb.getLong();
        r.steps   = hb.getLong();

        long off = 4 + 32;

        byte[] regsRaw = nativeRead(ptr + off, 33 * 8);
        ByteBuffer rb = ByteBuffer.wrap(regsRaw).order(ByteOrder.LITTLE_ENDIAN);
        for (int i = 0; i < 33; i++) r.regs[i] = rb.getLong();
        off += 33 * 8;

        byte[] vecRaw = nativeRead(ptr + off, 32 * 4 * 8);
        ByteBuffer vb = ByteBuffer.wrap(vecRaw).order(ByteOrder.LITTLE_ENDIAN);
        for (int i = 0; i < 32; i++)
            for (int j = 0; j < 4; j++)
                r.vecRegs[i][j] = vb.getDouble();
        off += 32 * 4 * 8;

        byte[] memLenRaw = nativeRead(ptr + off, 8);
        long memLen = ByteBuffer.wrap(memLenRaw)
                .order(ByteOrder.LITTLE_ENDIAN).getLong();
        off += 8;
        if (memLen > 0) {
            r.mem = nativeRead(ptr + off, (int) memLen);
            off += memLen;
        } else {
            r.mem = new byte[0];
        }

        byte[] outLenRaw = nativeRead(ptr + off, 8);
        long outLen = ByteBuffer.wrap(outLenRaw)
                .order(ByteOrder.LITTLE_ENDIAN).getLong();
        off += 8;
        if (outLen > 0) {
            byte[] outData = nativeRead(ptr + off, (int) outLen);
            r.output = new String(outData, StandardCharsets.UTF_8);
            off += outLen;
        } else {
            r.output = "";
        }

        byte[] errLenRaw = nativeRead(ptr + off, 2);
        int errLen = ByteBuffer.wrap(errLenRaw)
                .order(ByteOrder.LITTLE_ENDIAN).getShort() & 0xFFFF;
        off += 2;
        if (errLen > 0) {
            byte[] errData = nativeRead(ptr + off, errLen);
            r.error = new String(errData, StandardCharsets.UTF_8);
        }

        if (r.status == 2 && r.error == null) r.error = "unsupported";
        else if (r.status == 3 && (r.error == null || r.error.isEmpty()))
            r.error = "runtime error";

        return r;
    }

    private static byte[] nativeRead(long addr, int len) {
        if (len <= 0) return new byte[0];
        return codecin_read(addr, len);
    }

    // ==================== CROM ====================

    public static byte[] cromPack(byte[] mem, boolean compress) {
        if (!sLoaded) return null;
        if (mem == null) mem = new byte[0];

        int[] outLen = new int[1];
        long ptr = 0;
        try {
            ptr = codecin_crom_pack(mem, mem.length, compress ? 1 : 0, outLen);
            if (ptr == 0) return null;
            return nativeRead(ptr, outLen[0]);
        } catch (Throwable e) {
            sLoadError = e.toString();
            return null;
        } finally {
            if (ptr != 0) {
                try { codecin_free(ptr); } catch (Throwable ignored) {}
            }
        }
    }

    public static byte[] cromUnpack(byte[] data) {
        if (!sLoaded) return null;
        if (data == null || data.length == 0) return null;

        int[] memLen = new int[1];
        int[] flags = new int[1];
        long ptr = 0;
        try {
            ptr = codecin_crom_unpack(data, data.length, memLen, flags);
            if (ptr == 0) return null;
            return nativeRead(ptr, memLen[0]);
        } catch (Throwable e) {
            sLoadError = e.toString();
            return null;
        } finally {
            if (ptr != 0) {
                try { codecin_free(ptr); } catch (Throwable ignored) {}
            }
        }
    }
}