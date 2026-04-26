package miao.byusi.properos.two.app;

import android.util.Log;
import fi.iki.elonen.NanoHTTPD;
import okhttp3.OkHttpClient;
import okhttp3.Request;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.URLDecoder;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.TimeUnit;

public class startAppUI extends NanoHTTPD {

    private static final String TAG = "startAppUI";
    
    private static startAppUI instance;
    private String rootPath;
    private static OkHttpClient okHttpClient;
    
    private static final Map<String, String> MIME_TYPES = new HashMap<String, String>();
    static {
        MIME_TYPES.put("html", "text/html");
        MIME_TYPES.put("htm", "text/html");
        MIME_TYPES.put("css", "text/css");
        MIME_TYPES.put("js", "application/javascript");
        MIME_TYPES.put("json", "application/json");
        MIME_TYPES.put("xml", "application/xml");
        MIME_TYPES.put("png", "image/png");
        MIME_TYPES.put("jpg", "image/jpeg");
        MIME_TYPES.put("jpeg", "image/jpeg");
        MIME_TYPES.put("gif", "image/gif");
        MIME_TYPES.put("svg", "image/svg+xml");
        MIME_TYPES.put("ico", "image/x-icon");
        MIME_TYPES.put("txt", "text/plain");
        MIME_TYPES.put("mp3", "audio/mpeg");
        MIME_TYPES.put("mp4", "video/mp4");
        MIME_TYPES.put("webm", "video/webm");
        MIME_TYPES.put("pdf", "application/pdf");
        MIME_TYPES.put("zip", "application/zip");
        MIME_TYPES.put("wasm", "application/wasm");
    }

    private startAppUI(int port, String rootPath) {
        super(port);
        this.rootPath = rootPath;
    }

    private static synchronized OkHttpClient getOkHttpClient() {
        if (okHttpClient == null) {
            okHttpClient = new OkHttpClient.Builder()
                    .connectTimeout(30, TimeUnit.SECONDS)
                    .readTimeout(30, TimeUnit.SECONDS)
                    .writeTimeout(30, TimeUnit.SECONDS)
                    .build();
        }
        return okHttpClient;
    }

    public static boolean startServer(String url, int port) {
        try {
            Log.i(TAG, "startServer called with url: " + url + ", port: " + port);
            
            if (instance != null) {
                stopServer();
            }
            
            if (url == null || url.trim().isEmpty()) {
                Log.e(TAG, "url is null or empty");
                return false;
            }
            
            if (port <= 0 || port > 65535) {
                Log.e(TAG, "invalid port: " + port);
                return false;
            }
            
            File rootDir = new File(url);
            if (!rootDir.exists()) {
                Log.e(TAG, "directory not exists: " + url);
                return false;
            }
            
            if (!rootDir.isDirectory()) {
                Log.e(TAG, "path is not directory: " + url);
                return false;
            }
            
            instance = new startAppUI(port, url);
            instance.start();
            Log.i(TAG, "server started successfully on port " + port);
            return true;
            
        } catch (Exception e) {
            Log.e(TAG, "startServer failed: " + e.getMessage());
            e.printStackTrace();
            return false;
        }
    }

    public static boolean stopServer() {
        try {
            if (instance != null) {
                instance.stop();
                instance = null;
                Log.i(TAG, "server stopped");
                return true;
            }
            return false;
        } catch (Exception e) {
            Log.e(TAG, "stopServer failed: " + e.getMessage());
            return false;
        }
    }

    public static boolean isServerRunning() {
        return instance != null;
    }

    public static int getServerPort() {
        return instance != null ? instance.getListeningPort() : -1;
    }

    public static String getServerRootPath() {
        return instance != null ? instance.rootPath : null;
    }

    public static String httpGet(String url) {
        try {
            OkHttpClient client = getOkHttpClient();
            Request request = new Request.Builder().url(url).get().build();
            okhttp3.Response response = client.newCall(request).execute();
            try {
                if (response.isSuccessful() && response.body() != null) {
                    return response.body().string();
                }
                return null;
            } finally {
                if (response.body() != null) {
                    response.body().close();
                }
            }
        } catch (Exception e) {
            Log.e(TAG, "httpGet error: " + e.getMessage());
            return null;
        }
    }

    public static String httpPost(String url, String body) {
        try {
            OkHttpClient client = getOkHttpClient();
            okhttp3.MediaType mediaType = okhttp3.MediaType.parse("application/json; charset=utf-8");
            okhttp3.RequestBody requestBody = okhttp3.RequestBody.create(mediaType, body);
            Request request = new Request.Builder().url(url).post(requestBody).build();
            okhttp3.Response response = client.newCall(request).execute();
            try {
                if (response.isSuccessful() && response.body() != null) {
                    return response.body().string();
                }
                return null;
            } finally {
                if (response.body() != null) {
                    response.body().close();
                }
            }
        } catch (Exception e) {
            Log.e(TAG, "httpPost error: " + e.getMessage());
            return null;
        }
    }

    @Override
    public Response serve(IHTTPSession session) {
        try {
            String uri = session.getUri();
            
            if (uri.contains("..")) {
                return newFixedLengthResponse(Response.Status.FORBIDDEN, "text/plain", "Access Denied");
            }
            
            String filePath = buildFilePath(uri);
            File file = new File(filePath);
            
            if (!file.exists()) {
                return newFixedLengthResponse(Response.Status.NOT_FOUND, "text/plain", "404 Not Found");
            }
            
            if (file.isDirectory()) {
                File indexFile = new File(file, "index.html");
                if (indexFile.exists()) {
                    file = indexFile;
                } else {
                    return serveDirectoryListing(file, uri);
                }
            }
            
            String rangeHeader = session.getHeaders().get("range");
            if (rangeHeader != null && rangeHeader.startsWith("bytes=")) {
                return handleRangeRequest(file, rangeHeader);
            }
            
            String mimeType = getMimeType(filePath);
            InputStream inputStream = new FileInputStream(file);
            long fileSize = file.length();
            
            Response response = newFixedLengthResponse(Response.Status.OK, mimeType, inputStream, fileSize);
            response.addHeader("Access-Control-Allow-Origin", "*");
            
            return response;
            
        } catch (Exception e) {
            Log.e(TAG, "serve error: " + e.getMessage());
            return newFixedLengthResponse(Response.Status.INTERNAL_ERROR, "text/plain", "500 Internal Error");
        }
    }

    private Response handleRangeRequest(File file, String rangeHeader) throws IOException {
        String rangeValue = rangeHeader.substring("bytes=".length());
        String[] ranges = rangeValue.split("-");
        
        long fileSize = file.length();
        long start = ranges[0].isEmpty() ? 0 : Long.parseLong(ranges[0]);
        long end = ranges.length > 1 && !ranges[1].isEmpty() ? Long.parseLong(ranges[1]) : fileSize - 1;
        
        if (end >= fileSize) {
            end = fileSize - 1;
        }
        
        long contentLength = end - start + 1;
        String mimeType = getMimeType(file.getName());
        
        InputStream inputStream = new FileInputStream(file);
        inputStream.skip(start);
        
        Response response = newFixedLengthResponse(Response.Status.PARTIAL_CONTENT, mimeType, inputStream, contentLength);
        response.addHeader("Content-Range", "bytes " + start + "-" + end + "/" + fileSize);
        response.addHeader("Access-Control-Allow-Origin", "*");
        
        return response;
    }

    private String buildFilePath(String uri) throws IOException {
        int queryIndex = uri.indexOf('?');
        if (queryIndex > 0) {
            uri = uri.substring(0, queryIndex);
        }
        
        if (uri.length() > 1 && uri.endsWith("/")) {
            uri = uri.substring(0, uri.length() - 1);
        }
        
        String decodedUri = URLDecoder.decode(uri, "UTF-8");
        
        if (decodedUri.equals("/") || decodedUri.isEmpty()) {
            String indexPath = rootPath + File.separator + "index.html";
            if (new File(indexPath).exists()) {
                return indexPath;
            }
            return rootPath;
        }
        
        return rootPath + decodedUri;
    }

    private String getMimeType(String filePath) {
        int lastDot = filePath.lastIndexOf('.');
        if (lastDot >= 0) {
            String ext = filePath.substring(lastDot + 1).toLowerCase();
            String mime = MIME_TYPES.get(ext);
            if (mime != null) {
                return mime;
            }
        }
        return "application/octet-stream";
    }

    private Response serveDirectoryListing(File directory, String uri) {
        StringBuilder html = new StringBuilder();
        html.append("<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Index of ");
        html.append(escapeHtml(uri)).append("</title>");
        html.append("<style>body{font-family:monospace;margin:20px;}a{text-decoration:none;color:#0066cc;}</style>");
        html.append("</head><body>");
        html.append("<h1>Index of ").append(escapeHtml(uri)).append("</h1><hr><ul>");
        
        if (!uri.equals("/") && !uri.isEmpty()) {
            String parentPath;
            int lastSlash = uri.lastIndexOf('/');
            if (lastSlash <= 0) {
                parentPath = "/";
            } else {
                parentPath = uri.substring(0, lastSlash);
            }
            html.append("<li><a href=\"").append(parentPath).append("\">..</a></li>");
        }
        
        File[] files = directory.listFiles();
        if (files != null) {
            for (File file : files) {
                String fileName = file.getName();
                String fileUri = uri.endsWith("/") ? uri + fileName : uri + "/" + fileName;
                if (file.isDirectory()) {
                    html.append("<li><a href=\"").append(fileUri).append("/\">").append(escapeHtml(fileName)).append("/</a></li>");
                } else {
                    html.append("<li><a href=\"").append(fileUri).append("\">").append(escapeHtml(fileName)).append("</a></li>");
                }
            }
        }
        
        html.append("</ul><hr><p>HTTP Server</p></body></html>");
        
        Response response = newFixedLengthResponse(Response.Status.OK, "text/html", html.toString());
        response.addHeader("Access-Control-Allow-Origin", "*");
        return response;
    }

    private String escapeHtml(String text) {
        if (text == null) return "";
        return text.replace("&", "&amp;")
                   .replace("<", "&lt;")
                   .replace(">", "&gt;");
    }
}