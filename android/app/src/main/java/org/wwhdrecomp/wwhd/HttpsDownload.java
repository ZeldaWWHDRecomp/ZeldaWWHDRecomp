package org.wwhdrecomp.wwhd;

import java.io.InputStream;
import java.io.OutputStream;
import java.net.URL;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import javax.net.ssl.HttpsURLConnection;

// No Android APIs: the bounded transfer can also be exercised by host-side tests.
public final class HttpsDownload {
    private static URL checked(String value) throws Exception {
        URL url = new URL(value);
        if (!"https".equals(url.getProtocol()) || url.getHost().isEmpty()
                || url.getUserInfo() != null || url.getRef() != null || value.length() > 2048)
            throw new Exception("Download URL must use HTTPS");
        for (int i = 0; i < value.length(); ++i)
            if (value.charAt(i) <= 32 || value.charAt(i) == 127 || value.charAt(i) == '\\')
                throw new Exception("Invalid HTTPS download URL");
        return url;
    }
    public static String download(String address, String destination, long limit) {
        Path path = new java.io.File(destination).toPath();
        boolean created = false, complete = false;
        HttpsURLConnection connection = null;
        try {
            URL url = checked(address);
            if (limit <= 0) throw new Exception("Invalid download size limit");
            long started = System.nanoTime();
            try (OutputStream output = Files.newOutputStream(path, StandardOpenOption.CREATE_NEW, StandardOpenOption.WRITE)) {
                created = true;
                for (int redirects = 0;; ++redirects) {
                    connection = (HttpsURLConnection) url.openConnection();
                    connection.setInstanceFollowRedirects(false);
                    connection.setConnectTimeout(15000);
                    connection.setReadTimeout(30000);
                    connection.setUseCaches(false);
                    int status = connection.getResponseCode();
                    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
                        String next = connection.getHeaderField("Location");
                        if (redirects >= 5 || next == null) throw new Exception("HTTPS download redirect refused");
                        url = checked(new URL(url, next).toString());
                        connection.disconnect(); connection = null;
                        if (System.nanoTime() - started > 120000000000L) throw new Exception("HTTPS download timed out");
                        continue;
                    }
                    if (status != 200) throw new Exception("HTTPS server did not return a successful response");
                    long received = 0;
                    try (InputStream input = connection.getInputStream()) {
                        byte[] buffer = new byte[16384];
                        for (;;) {
                            if (System.nanoTime() - started > 120000000000L) throw new Exception("HTTPS download timed out");
                            int bytes = input.read(buffer);
                            if (bytes < 0) break;
                            if (bytes > limit - received) throw new Exception("Download exceeds size limit");
                            output.write(buffer, 0, bytes); received += bytes;
                        }
                    }
                    break;
                }
            }
            complete = true; return "";
        } catch (Exception error) {
            // Do not expose URLs, local paths or server messages in the UI.
            String message = error.getMessage();
            if (message != null && (message.startsWith("Download ") || message.startsWith("HTTPS ") || message.equals("Invalid download size limit")))
                return message;
            return "HTTPS download failed; check the connection and try again";
        } finally {
            if (connection != null) connection.disconnect();
            if (created && !complete) try { Files.deleteIfExists(path); } catch (Exception ignored) {}
        }
    }
    private HttpsDownload() {}
}
