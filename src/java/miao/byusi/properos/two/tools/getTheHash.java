package miao.byusi.properos.two.tools;

import android.content.Context;
import android.content.res.AssetManager;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.security.MessageDigest;

/**
 * getTheHash - 计算文件Hash值，返回JSON格式数据存入sss.hash
 * 支持路径格式：
 * %xxx - SD卡根目录
 * @xxx - assets目录
 * $xxx - 应用私有目录
 */
public class getTheHash {
    
    private static Context context = null;
    
    /**
     * 设置Context（必须在载入事件中先调用）
     * @param ctx Activity对象
     */
    public static void init(Context ctx) {
        context = ctx;
    }
    
    /**
     * 计算文件的所有Hash值(MD5/SHA1/SHA256)，结果以JSON格式存入sss.hash
     * @param filePath 文件路径（支持% @ $开头的iApp路径）
     * @param sss 裕语言全局变量HashMap
     */
    public static void getFileHash(String filePath, java.util.HashMap<String, Object> sss) {
        if (filePath == null || filePath.isEmpty()) {
            sss.put("hash", "{\"md5\":null,\"sha1\":null,\"sha256\":null,\"error\":\"文件路径为空\"}");
            return;
        }
        
        try {
            byte[] fileData = readFileToBytes(filePath);
            if (fileData == null) {
                sss.put("hash", "{\"md5\":null,\"sha1\":null,\"sha256\":null,\"error\":\"读取文件失败\"}");
                return;
            }
            
            String md5 = calculateHash(fileData, "MD5");
            String sha1 = calculateHash(fileData, "SHA-1");
            String sha256 = calculateHash(fileData, "SHA-256");
            
            String json = "{\"md5\":\"" + md5 + "\",\"sha1\":\"" + sha1 + "\",\"sha256\":\"" + sha256 + "\"}";
            sss.put("hash", json);
            
        } catch (Exception e) {
            e.printStackTrace();
            sss.put("hash", "{\"md5\":null,\"sha1\":null,\"sha256\":null,\"error\":\"" + e.getMessage() + "\"}");
        }
    }
    
    /**
     * 根据路径读取文件到字节数组
     * 支持格式：
     * %xxx - SD卡根目录
     * @xxx - assets目录
     * $xxx - 应用私有目录
     */
    private static byte[] readFileToBytes(String filePath) {
        try {
            InputStream is = getFileInputStream(filePath);
            if (is == null) {
                return null;
            }
            
            byte[] buffer = new byte[is.available()];
            is.read(buffer);
            is.close();
            return buffer;
            
        } catch (Exception e) {
            e.printStackTrace();
            return null;
        }
    }
    
    /**
     * 根据路径获取文件输入流
     * @param filePath 文件路径
     * @return InputStream
     */
    private static InputStream getFileInputStream(String filePath) {
        try {
            if (filePath.startsWith("%")) {
                // SD卡路径
                String realPath = getSdCardPath() + filePath.substring(1);
                File file = new File(realPath);
                if (file.exists()) {
                    return new FileInputStream(file);
                }
            } 
            else if (filePath.startsWith("@")) {
                // assets目录
                if (context != null) {
                    AssetManager am = context.getAssets();
                    return am.open(filePath.substring(1));
                }
            } 
            else if (filePath.startsWith("$")) {
                // 应用私有目录
                if (context != null) {
                    String realPath = context.getFilesDir().getAbsolutePath() + "/" + filePath.substring(1);
                    File file = new File(realPath);
                    if (file.exists()) {
                        return new FileInputStream(file);
                    }
                }
            } 
            else {
                // 普通路径
                File file = new File(filePath);
                if (file.exists()) {
                    return new FileInputStream(file);
                }
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
        return null;
    }
    
    /**
     * 获取SD卡根目录路径
     */
    private static String getSdCardPath() {
        return android.os.Environment.getExternalStorageDirectory().getAbsolutePath() + "/";
    }
    
    /**
     * 计算Hash值
     */
    private static String calculateHash(byte[] data, String algorithm) {
        try {
            MessageDigest md = MessageDigest.getInstance(algorithm);
            byte[] digest = md.digest(data);
            StringBuilder hexString = new StringBuilder();
            for (byte b : digest) {
                String hex = Integer.toHexString(0xff & b);
                if (hex.length() == 1) {
                    hexString.append('0');
                }
                hexString.append(hex);
            }
            return hexString.toString();
        } catch (Exception e) {
            e.printStackTrace();
            return null;
        }
    }
}