package org.wwhdrecomp.wwhd;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.Comparator;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

public final class ResourceVerifierTest {
    private static void require(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }
    private static byte[] archive(String name, byte[] data) throws Exception {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(bytes)) {
            zip.putNextEntry(new ZipEntry(name));
            zip.write(data);
            zip.closeEntry();
        }
        return bytes.toByteArray();
    }
    private static boolean matches(byte[] zip, Path root, boolean marker) throws Exception {
        return ResourceVerifier.matches(new ByteArrayInputStream(zip), root.toFile(), marker, () -> {});
    }
    public static void main(String[] args) throws Exception {
        Path root = Files.createTempDirectory("wwhd-resource-test");
        try {
            byte[] bytes = new byte[300000];
            for (int i = 0; i < bytes.length; i++) bytes[i] = (byte)(i * 31);
            byte[] zip = archive("nested/resource.bin", bytes);
            Path file = root.resolve("nested/resource.bin");
            Files.createDirectories(file.getParent());
            Files.write(file, bytes);
            require(matches(zip, root, false), "valid resource tree rejected");
            Files.write(root.resolve("complete"), new byte[]{1});
            require(ResourceVerifier.complete(root.toFile()), "valid marker rejected");
            require(matches(zip, root, true), "completion marker rejected");
            require(!matches(zip, root, false), "unexpected marker accepted");
            Files.write(root.resolve("complete"), new byte[]{0});
            require(!ResourceVerifier.complete(root.toFile()), "wrong marker accepted");
            Files.write(root.resolve("complete"), new byte[]{1, 1});
            require(!ResourceVerifier.complete(root.toFile()), "extended marker accepted");
            Files.write(root.resolve("complete"), new byte[]{1});
            byte[] damaged = bytes.clone();damaged[150000] ^= 1;
            Files.write(file, damaged);
            require(!matches(zip, root, true), "same-length damage accepted");
            Files.write(file, Arrays.copyOf(bytes, bytes.length - 1));
            require(!matches(zip, root, true), "truncated resource accepted");
            Files.write(file, Arrays.copyOf(bytes, bytes.length + 1));
            require(!matches(zip, root, true), "extended resource accepted");
            Files.delete(file);
            require(!matches(zip, root, true), "missing resource accepted");
            Files.write(file, bytes);
            Path extra = root.resolve("nested/unbundled.py");Files.writeString(extra, "extra");
            require(!matches(zip, root, true), "unbundled file accepted");
            Files.delete(extra);
            Files.createDirectories(root.resolve("unbundled-directory"));
            require(!matches(zip, root, true), "unbundled directory accepted");
            Files.delete(root.resolve("unbundled-directory"));
            int[] checkpoints = {0};
            final class Paused extends Exception {}
            try {
                ResourceVerifier.matches(new ByteArrayInputStream(zip), root.toFile(), true, () -> {
                    if (++checkpoints[0] == 8) throw new Paused();
                });
                throw new AssertionError("mid-file pause ignored");
            } catch (Paused expected) { }
            require(Arrays.equals(Files.readAllBytes(file), bytes), "verification pause changed file");
            require(ResourceVerifier.complete(root.toFile()), "verification pause changed marker");
            require(matches(zip, root, true), "retry after verification pause failed");
            try {
                matches(archive("../outside", bytes), root, true);
                throw new AssertionError("escaping archive path accepted");
            } catch (java.io.IOException expected) { }
            Files.delete(file);Files.createSymbolicLink(file, root.resolve("complete"));
            require(!matches(zip, root, true), "symbolic resource accepted");
            Files.delete(file);Files.write(file, bytes);
            Path moved = root.resolve("saved-resource");Files.move(file, moved);
            Files.delete(file.getParent());Files.createSymbolicLink(file.getParent(), root.getParent());
            require(!matches(zip, root, true), "escaping directory link accepted");
            Files.delete(file.getParent());Files.createDirectory(file.getParent());Files.move(moved, file);
            require(matches(zip, root, true), "restored resource tree rejected");
            System.out.println("Resource verification: content, inventory, markers, symlinks and mid-file pause/retry passed");
        } finally {
            try (var paths = Files.walk(root)) {
                for (Path path : paths.sorted(Comparator.reverseOrder()).toList()) Files.delete(path);
            }
        }
    }
}
