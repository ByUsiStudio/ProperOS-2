package miao.byusi.properos.two.toolkit;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;

/**
 * 网络请求工具类
 * 对应裕语言的 hs 函数
 */
public class HttpUtils {
    
    private static final int CONNECT_TIMEOUT = 15000;
    private static final int READ_TIMEOUT = 15000;
    
    /**
     * 发送HTTP请求
     * 对应 hs(url, postData, encoding)
     */
    public static String httpRequest(String urlString, String postData, String encoding) {
        HttpURLConnection connection = null;
        BufferedReader reader = null;
        
        try {
            URL url = new URL(urlString);
            connection = (HttpURLConnection) url.openConnection();
            connection.setConnectTimeout(CONNECT_TIMEOUT);
            connection.setReadTimeout(READ_TIMEOUT);
            
            if (postData != null && !postData.isEmpty()) {
                // POST请求
                connection.setRequestMethod("POST");
                connection.setDoOutput(true);
                connection.setRequestProperty("Content-Type", "application/x-www-form-urlencoded");
                
                OutputStream os = connection.getOutputStream();
                os.write(postData.getBytes(encoding));
                os.flush();
                os.close();
            } else {
                // GET请求
                connection.setRequestMethod("GET");
            }
            
            int responseCode = connection.getResponseCode();
            if (responseCode == HttpURLConnection.HTTP_OK) {
                String charset = (encoding != null) ? encoding : "UTF-8";
                reader = new BufferedReader(new InputStreamReader(connection.getInputStream(), charset));
                
                StringBuilder response = new StringBuilder();
                String line;
                while ((line = reader.readLine()) != null) {
                    response.append(line);
                }
                
                System.out.println("[HS] 请求成功: " + urlString);
                return response.toString();
            } else {
                System.out.println("[HS] 请求失败, 响应码: " + responseCode);
                return null;
            }
            
        } catch (Exception e) {
            System.out.println("[HS] 请求异常: " + e.getMessage());
            return null;
        } finally {
            try {
                if (reader != null) reader.close();
                if (connection != null) connection.disconnect();
            } catch (Exception e) {
                e.printStackTrace();
            }
        }
    }
}