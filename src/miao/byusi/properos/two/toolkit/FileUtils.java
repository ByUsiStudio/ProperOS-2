package miao.byusi.properos.two.toolkit;

import java.io.*;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

/**
 * 文件操作工具类
 * 对应裕语言的 fuz, fuzs, fc, fr 等函数
 */
public class FileUtils {
    
    /**
     * 解压ZIP中的单个文件
     * 对应 fuz(zipPath, fileName, destDir)
     */
    public static boolean unzipSingleFile(String zipPath, String fileName, String destDir) {
        ZipFile zipFile = null;
        try {
            zipFile = new ZipFile(zipPath);
            ZipEntry entry = zipFile.getEntry(fileName);
            if (entry == null) {
                System.out.println("[FUZ] 文件不存在: " + fileName);
                return false;
            }
            
            File destFile = new File(destDir, fileName);
            if (!destFile.getParentFile().exists()) {
                destFile.getParentFile().mkdirs();
            }
            
            InputStream is = zipFile.getInputStream(entry);
            FileOutputStream fos = new FileOutputStream(destFile);
            
            byte[] buffer = new byte[8192];
            int len;
            while ((len = is.read(buffer)) != -1) {
                fos.write(buffer, 0, len);
            }
            
            fos.close();
            is.close();
            
            System.out.println("[FUZ] 解压成功: " + fileName + " -> " + destFile.getAbsolutePath());
            return true;
            
        } catch (Exception e) {
            System.out.println("[FUZ] 解压失败: " + e.getMessage());
            return false;
        } finally {
            try {
                if (zipFile != null) zipFile.close();
            } catch (IOException e) {
                e.printStackTrace();
            }
        }
    }
    
    /**
     * 解压整个ZIP文件
     * 对应 fuzs(zipPath, destDir, overwrite)
     */
    public static boolean unzipFull(String zipPath, String destDir, boolean overwrite) {
        ZipInputStream zis = null;
        try {
            File destDirectory = new File(destDir);
            if (!destDirectory.exists()) {
                destDirectory.mkdirs();
            }
            
            zis = new ZipInputStream(new FileInputStream(zipPath));
            ZipEntry entry;
            
            while ((entry = zis.getNextEntry()) != null) {
                File outFile = new File(destDir, entry.getName());
                
                // 处理目录
                if (entry.isDirectory()) {
                    outFile.mkdirs();
                    continue;
                }
                
                // 处理文件
                if (!overwrite && outFile.exists()) {
                    System.out.println("[FUZS] 文件已存在，跳过: " + entry.getName());
                    continue;
                }
                
                if (!outFile.getParentFile().exists()) {
                    outFile.getParentFile().mkdirs();
                }
                
                FileOutputStream fos = new FileOutputStream(outFile);
                byte[] buffer = new byte[8192];
                int len;
                while ((len = zis.read(buffer)) != -1) {
                    fos.write(buffer, 0, len);
                }
                fos.close();
                zis.closeEntry();
            }
            
            System.out.println("[FUZS] 完整解压成功: " + zipPath + " -> " + destDir);
            return true;
            
        } catch (Exception e) {
            System.out.println("[FUZS] 解压失败: " + e.getMessage());
            return false;
        } finally {
            try {
                if (zis != null) zis.close();
            } catch (IOException e) {
                e.printStackTrace();
            }
        }
    }
    
    /**
     * 复制文件
     * 对应 fc(src, dest, overwrite)
     */
    public static boolean copyFile(String srcPath, String destPath, boolean overwrite) {
        File src = new File(srcPath);
        File dest = new File(destPath);
        
        if (!src.exists()) {
            System.out.println("[FC] 源文件不存在: " + srcPath);
            return false;
        }
        
        if (dest.exists() && !overwrite) {
            System.out.println("[FC] 目标文件已存在，不覆盖: " + destPath);
            return false;
        }
        
        try {
            if (!dest.getParentFile().exists()) {
                dest.getParentFile().mkdirs();
            }
            
            FileInputStream fis = new FileInputStream(src);
            FileOutputStream fos = new FileOutputStream(dest);
            
            byte[] buffer = new byte[8192];
            int len;
            while ((len = fis.read(buffer)) != -1) {
                fos.write(buffer, 0, len);
            }
            
            fis.close();
            fos.close();
            
            System.out.println("[FC] 复制成功: " + srcPath + " -> " + destPath);
            return true;
            
        } catch (Exception e) {
            System.out.println("[FC] 复制失败: " + e.getMessage());
            return false;
        }
    }
    
    /**
     * 读取文本文件
     * 对应 fr(filePath)
     */
    public static String readTextFile(String filePath) {
        File file = new File(filePath);
        if (!file.exists()) {
            System.out.println("[FR] 文件不存在: " + filePath);
            return null;
        }
        
        StringBuilder content = new StringBuilder();
        BufferedReader reader = null;
        
        try {
            reader = new BufferedReader(new InputStreamReader(new FileInputStream(file), "UTF-8"));
            String line;
            while ((line = reader.readLine()) != null) {
                content.append(line);
            }
            
            System.out.println("[FR] 读取成功: " + filePath);
            return content.toString();
            
        } catch (Exception e) {
            System.out.println("[FR] 读取失败: " + e.getMessage());
            return null;
        } finally {
            try {
                if (reader != null) reader.close();
            } catch (IOException e) {
                e.printStackTrace();
            }
        }
    }
    
    /**
     * 写入文本文件
     * 对应 fw(filePath, content)
     */
    public static boolean writeTextFile(String filePath, String content) {
        File file = new File(filePath);
        
        try {
            if (!file.getParentFile().exists()) {
                file.getParentFile().mkdirs();
            }
            
            FileOutputStream fos = new FileOutputStream(file);
            fos.write(content.getBytes("UTF-8"));
            fos.close();
            
            System.out.println("[FW] 写入成功: " + filePath);
            return true;
            
        } catch (Exception e) {
            System.out.println("[FW] 写入失败: " + e.getMessage());
            return false;
        }
    }
    
    /**
     * 文件是否存在
     * 对应 fe(filePath)
     */
    public static boolean fileExists(String filePath) {
        File file = new File(filePath);
        return file.exists();
    }
    
    /**
     * 创建目录
     */
    public static boolean createDir(String dirPath) {
        File dir = new File(dirPath);
        if (!dir.exists()) {
            return dir.mkdirs();
        }
        return true;
    }
}