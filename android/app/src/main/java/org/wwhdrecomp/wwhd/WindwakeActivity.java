// SPDX-License-Identifier: MPL-2.0
package org.wwhdrecomp.wwhd;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;
import java.io.File;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Native launcher. Game preparation, controller input and rendering use the pinned HD runtime. */
public final class WindwakeActivity extends Activity {
    private static final int INK = 0xFFF6F1DB, MUTED = 0xFFA8C8C5, GOLD = 0xFFE8C97E;
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private String page = "INÍCIO", status = "Verificando a instalação…", profile = WindwakeSettings.NAMES[0];
    private boolean ready, busy;
    private Map<String, String> touchValues = java.util.Collections.emptyMap();
    private LinearLayout body;
    private TextView statusView;
    interface Job { String run() throws Exception; }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        if (state != null) page = state.getString("page", "INÍCIO");
        getWindow().setStatusBarColor(0xFF062E37);
        getWindow().setNavigationBarColor(0xFF062E37);
        draw();
    }

    @Override protected void onResume() { super.onResume(); refresh(); }
    @Override protected void onSaveInstanceState(Bundle state) { state.putString("page", page); super.onSaveInstanceState(state); }
    @Override protected void onDestroy() { worker.shutdownNow(); super.onDestroy(); }

    private void draw() {
        FrameLayout frame = new FrameLayout(this);
        frame.addView(new Sea(this), new FrameLayout.LayoutParams(-1, -1));
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        body = column();
        body.setPadding(dp(24), dp(22), dp(24), dp(28));
        scroll.addView(body);
        frame.addView(scroll);
        final LinearLayout content = body;
        frame.setOnApplyWindowInsetsListener((view, insets) -> {
            Insets safe = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            content.setPadding(dp(24) + safe.left, dp(22) + safe.top, dp(24) + safe.right, dp(28) + safe.bottom);
            return insets;
        });
        setContentView(frame);

        LinearLayout brand = row();
        TextView wordmark = text("W  /  WINDWAKE", 18, GOLD);
        wordmark.setLetterSpacing(0.2f);
        brand.addView(wordmark, new LinearLayout.LayoutParams(0, -2, 1));
        TextView edition = text("ANDROID\nEDIÇÃO PESSOAL", 10, MUTED);
        edition.setGravity(Gravity.END);
        brand.addView(edition);
        body.addView(brand);
        gap(20);

        LinearLayout nav = row();
        for (String tab : new String[] {"INÍCIO", "PERFIS", "TOUCH", "SAVES"}) {
            Button item = button(tab, false, () -> { page = tab; draw(); });
            item.setTextSize(11);
            item.setTextColor(page.equals(tab) ? GOLD : MUTED);
            item.setPadding(0, 0, 0, 0);
            nav.addView(item, new LinearLayout.LayoutParams(0, dp(48), 1));
        }
        body.addView(nav);
        gap(22);
        switch (page) {
            case "PERFIS": profiles(); break;
            case "TOUCH": controls(); break;
            case "SAVES": saves(); break;
            default: home();
        }
        gap(28);
        TextView credit = text("WINDWAKE 0.2  /  WIND WAKER HD\nBase aberta: ZeldaWWHDRecomp v0.5.0 · MPL-2.0", 10, MUTED);
        credit.setLineSpacing(dp(4), 1);
        body.addView(credit);
    }

    private void home() {
        label("UMA NOVA VIAGEM");
        TextView hero = text("O grande mar\nestá chamando.", 44, INK);
        hero.setTypeface(Typeface.create("serif", Typeface.NORMAL));
        hero.setLineSpacing(0, 0.95f);
        body.addView(hero);
        gap(14);
        TextView intro = text("The Wind Waker HD, no seu Android.\nSeu jogo, seus controles, seu ritmo.", 16, MUTED);
        intro.setLineSpacing(dp(4), 1);
        body.addView(intro);
        gap(110);
        LinearLayout card = card();
        label(card, "PRONTO PARA ZARPAR?");
        statusView = text(status, 14, MUTED);
        card.addView(statusView);
        addButton(card, ready ? "JOGAR  →" : "PREPARAR MEU JOGO  →", true, this::play);
        addButton(card, "Gerenciar instalação", false, this::setup);
        body.addView(card);
        gap(12);
        LinearLayout device = card();
        device.addView(text("PERFIL ATIVO  /  " + profile, 12, GOLD));
        device.addView(text("Poco M8 Pro · Snapdragon 7s Gen 4\n30 FPS como ponto de partida · proteção térmica ativa", 13, MUTED));
        addButton(device, "Ajustar desempenho", false, () -> { page = "PERFIS"; draw(); });
        body.addView(device);
    }

    private void profiles() {
        title("Encontre seu\nvento favorável.", "DESEMPENHO");
        paragraph("Perfis pensados para o Poco M8 Pro com Snapdragon 7s Gen 4. A resolução dinâmica reduz a carga da GPU; o resultado depende da cena e da temperatura.");
        for (int i = 0; i < WindwakeSettings.NAMES.length; i++) {
            final int choice = i;
            LinearLayout card = card();
            card.addView(text(WindwakeSettings.NAMES[i], 21, INK));
            card.addView(text(WindwakeSettings.DETAILS[i], 14, MUTED));
            addButton(card, "Aplicar perfil", i == 0, () -> change(() -> {
                WindwakeSettings.update(settings(), WindwakeSettings.profile(choice), WindwakeSession::sync);
                return "Perfil aplicado. Ajustes adicionais continuam disponíveis dentro do jogo.";
            }));
            body.addView(card); gap(12);
        }
        paragraph("Todos mantêm a lógica a 30 FPS, VSync e proteção térmica normal. Interpolação de 60 FPS pode ser experimentada no menu do jogo após medir o desempenho.");
    }

    private void controls() {
        title("A aventura,\nna ponta dos dedos.", "CONTROLES TOUCH");
        paragraph("Deslize na área livre à direita para mover a câmera. O movimento fica à esquerda; sem analógico de câmera ocupando a tela. Ao mirar um item, deslize na área livre dos dois lados.");
        LinearLayout card = card();
        card.addView(text("Câmera e mira", 21, INK));
        SeekBar camera = sensitivity(card, "Sensibilidade da câmera", WindwakeTouchProfile.percent(touchValues.get("touchSwipeSpeed"), 100));
        SeekBar aim = sensitivity(card, "Sensibilidade da mira", WindwakeTouchProfile.percent(touchValues.get("windwakeAimSpeed"), 70));
        card.addView(text("100% é o padrão da câmera. A mira tem sensibilidade própria para ajustes mais precisos.", 14, MUTED));
        CheckBox fixed = option(card, "Analógico de movimento fixo", "1".equals(touchValues.get("windwakeFixedMove")));
        CheckBox tap = option(card, "Preparar item com um toque", !"0".equals(touchValues.get("touchItemTap")));
        card.addView(text("Toque no item para preparar, arraste para mirar e use o botão do item para a ação. B guarda o item. Você não precisa manter o gatilho pressionado.", 14, MUTED));
        CheckBox lock = option(card, "Travar alvo com um toque", !"0".equals(touchValues.get("touchZlLatch")));
        addButton(card, "Aplicar controles", true, () -> change(() -> {
            Map<String, String> changes = WindwakeSettings.mobileControls();
            changes.put("touchSwipeSpeed", String.format(java.util.Locale.ROOT, "%.2f", camera.getProgress() / 100f));
            changes.put("windwakeAimSpeed", String.format(java.util.Locale.ROOT, "%.2f", aim.getProgress() / 100f));
            changes.put("windwakeFixedMove", fixed.isChecked() ? "1" : "0");
            changes.put("touchItemTap", tap.isChecked() ? "1" : "0");
            changes.put("touchZlLatch", lock.isChecked() ? "1" : "0");
            WindwakeSettings.update(settings(), changes, WindwakeSession::sync);
            return "Controles salvos para o próximo início do jogo.";
        }));
        body.addView(card); gap(12);
        LinearLayout edit = card();
        edit.addView(text("Um layout para o celular", 21, INK));
        edit.addView(text("Botões coloridos à direita, gatilhos em cápsula e atalhos para bolsa, visão, mapa e configurações embaixo à esquerda. Segure o ícone de controle para mover, redimensionar ou mudar a transparência dos botões.", 14, MUTED));
        addButton(edit, "Jogar e ajustar controles", false, this::play);
        addButton(edit, "Restaurar layout padrão", false, () -> change(() -> {
            android.content.SharedPreferences prefs = getSharedPreferences("controls", MODE_PRIVATE);
            android.content.SharedPreferences.Editor changes = prefs.edit();
            String old = prefs.getString(WindwakeTouchProfile.LAYOUT_KEY, null);
            if (old != null) changes.putString(WindwakeTouchProfile.LAYOUT_KEY + "_backup", old);
            if (!changes.remove(WindwakeTouchProfile.LAYOUT_KEY).commit()) throw new java.io.IOException("Não foi possível restaurar o layout.");
            return "Layout padrão restaurado; a edição anterior ficou guardada em backup.";
        }));
        body.addView(edit);
    }

    private SeekBar sensitivity(LinearLayout parent, String label, int value) {
        TextView caption = text(label + " · " + value + "%", 16, INK);
        parent.addView(caption);
        SeekBar slider = new SeekBar(this);
        slider.setMin(25); slider.setMax(300); slider.setProgress(value);
        slider.setContentDescription(label); slider.setStateDescription(value + "%");
        slider.setPadding(dp(8), dp(12), dp(8), dp(12));
        slider.setProgressTintList(android.content.res.ColorStateList.valueOf(GOLD));
        slider.setThumbTintList(android.content.res.ColorStateList.valueOf(GOLD));
        slider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onProgressChanged(SeekBar bar, int progress, boolean user) {
                caption.setText(label + " · " + progress + "%"); bar.setStateDescription(progress + "%");
            }
            public void onStartTrackingTouch(SeekBar bar) { }
            public void onStopTrackingTouch(SeekBar bar) { }
        });
        parent.addView(slider, new LinearLayout.LayoutParams(-1, dp(56)));
        return slider;
    }

    private CheckBox option(LinearLayout parent, String title, boolean checked) {
        CheckBox box = new CheckBox(this); box.setText(title); box.setTextColor(INK);
        box.setTextSize(15); box.setChecked(checked); box.setMinHeight(dp(52));
        box.setButtonTintList(android.content.res.ColorStateList.valueOf(GOLD));
        parent.addView(box); return box;
    }

    private void saves() {
        title("Seu progresso\nsegue com você.", "SAVES E BACKUPS");
        paragraph("Exporte seu progresso ou importe um ZIP com user/cking.sav. Feche o jogo pelo menu antes de transferir os arquivos.");
        LinearLayout card = card();
        addButton(card, "Exportar save", true, () -> startActivity(new Intent(this, ExportSaveActivity.class)));
        addButton(card, "Importar save", false, () -> new AlertDialog.Builder(this)
            .setTitle("Importar um save")
            .setMessage("O progresso atual será guardado em um backup. O novo ZIP só será publicado depois de ser extraído e verificado por inteiro.")
            .setNegativeButton("Cancelar", null)
            .setPositiveButton("Escolher arquivo", (d, which) -> startActivity(new Intent(this, ImportSaveActivity.class))).show());
        body.addView(card); gap(14);
        paragraph("A importação troca a pasta completa de uma vez e mantém o save anterior. Se o processo for interrompido, o próximo início recupera a operação antes de abrir o jogo.");
    }

    private File settings() throws Exception {
        return new File(AndroidUserDirectories.config(WindwakeSession.root(this)), "settings.ini");
    }

    private void change(Job job) {
        if (busy) return;
        busy = true;
        worker.execute(() -> {
            String result;
            try (SessionLease lease = SessionLease.acquire(WindwakeSession.root(this))) { result = job.run(); }
            catch (Exception failure) { result = failure.getMessage(); }
            final String message = result;
            runOnUiThread(() -> {
                if (isDestroyed()) return;
                busy = false;
                Toast.makeText(this, message, Toast.LENGTH_LONG).show();
                refresh();
            });
        });
    }

    private void refresh() {
        worker.execute(() -> {
            String message, selectedProfile = WindwakeSettings.NAMES[0];
            Map<String, String> controls = java.util.Collections.emptyMap();
            boolean canPlay = false;
            try {
                File root = WindwakeSession.root(this);
                try (SessionLease lease = SessionLease.acquire(root)) {
                    WindwakeSession.store(root).recover();
                    WindwakeSettings.defaults(settings(), WindwakeSession::sync);
                } catch (java.io.IOException inUse) { /* The game's process owns its files while playing. */ }
                Map<String, String> options = WindwakeSettings.read(settings());
                controls = options;
                String id = options.get("windwakeProfile");
                if (id != null) {
                    int index = Integer.parseInt(id);
                    if (index >= 0 && index < WindwakeSettings.NAMES.length) selectedProfile = WindwakeSettings.NAMES[index];
                }
                canPlay = new File(AndroidGame.storage(this), "active.json").isFile() &&
                    !AndroidGame.needsRebuild(this) && AndroidGame.current(this) != null;
                File job = SetupStore.current(this);
                if (job != null && new File(job, "host.json").isFile() &&
                    !SetupStore.read(new File(job, "host.json")).optString("state").equals("complete")) canPlay = false;
                message = canPlay ? "Jogo verificado. Sua próxima viagem está pronta." :
                    "Selecione seu dump WUX, WUA ou a pasta extraída para preparar o jogo.";
            } catch (Exception failure) { message = "Preparação necessária: " + failure.getMessage(); }
            final String display = message, active = selectedProfile;
            final boolean launch = canPlay;
            final Map<String, String> currentControls = controls;
            runOnUiThread(() -> {
                if (isDestroyed()) return;
                boolean changed = ready != launch || !profile.equals(active) || !touchValues.equals(currentControls);
                touchValues = currentControls;
                ready = launch; profile = active; status = display;
                if (changed) draw(); else if (statusView != null) statusView.setText(status);
            });
        });
    }

    private void play() {
        if (!ready) { setup(); return; }
        startActivity(QuestActivity.gameIntent(this));
    }

    private void setup() {
        startActivity(new Intent(this, SetupActivity.class).putExtra(SetupActivity.EXTRA_SHOW_SETUP, true));
    }

    private int dp(float value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    private LinearLayout column() { LinearLayout v = new LinearLayout(this); v.setOrientation(LinearLayout.VERTICAL); return v; }
    private LinearLayout row() { LinearLayout v = new LinearLayout(this); v.setGravity(Gravity.CENTER_VERTICAL); return v; }
    private void gap(int size) { View v = new View(this); body.addView(v, new LinearLayout.LayoutParams(1, dp(size))); }
    private TextView text(String value, int size, int color) {
        TextView v = new TextView(this); v.setText(value); v.setTextSize(size); v.setTextColor(color);
        v.setPadding(0, dp(3), 0, dp(5)); return v;
    }
    private void label(String value) { label(body, value); }
    private void label(LinearLayout parent, String value) { TextView v = text(value, 10, GOLD); v.setLetterSpacing(0.15f); parent.addView(v); }
    private void title(String value, String eyebrow) { label(eyebrow); TextView v = text(value, 36, INK); v.setTypeface(Typeface.create("serif", 0)); body.addView(v); gap(12); }
    private void paragraph(String value) { TextView v = text(value, 15, MUTED); v.setLineSpacing(dp(4), 1); body.addView(v); gap(16); }
    private LinearLayout card() {
        LinearLayout v = column(); v.setPadding(dp(18), dp(16), dp(18), dp(12));
        GradientDrawable bg = new GradientDrawable(); bg.setColor(0xE6103B43); bg.setCornerRadius(dp(18)); bg.setStroke(dp(1), 0x445FA5A1); v.setBackground(bg); return v;
    }
    private Button button(String label, boolean primary, Runnable action) {
        Button v = new Button(this); v.setText(label); v.setAllCaps(false); v.setTextSize(14);
        v.setTextColor(primary ? 0xFF173B3D : INK); v.setMinHeight(dp(52));
        GradientDrawable bg = new GradientDrawable(); bg.setColor(primary ? GOLD : 0x173E8788); bg.setCornerRadius(dp(11));
        v.setBackground(bg); v.setOnClickListener(clicked -> action.run()); return v;
    }
    private void addButton(LinearLayout parent, String label, boolean primary, Runnable action) {
        Button v = button(label, primary, action); LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(-1, dp(54)); p.topMargin = dp(12); parent.addView(v, p);
    }

    /** Original vector artwork: no game imagery, animation loop or network requests. */
    private static final class Sea extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        Sea(android.content.Context context) { super(context); setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO); }
        @Override protected void onDraw(Canvas canvas) {
            float w = getWidth(), h = getHeight(), density = getResources().getDisplayMetrics().density;
            paint.setShader(new LinearGradient(0, 0, w, h, new int[] {0xFF062E37, 0xFF135758, 0xFF062C38}, null, Shader.TileMode.CLAMP));
            canvas.drawRect(0, 0, w, h, paint); paint.setShader(null);
            paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(density);
            for (int i = 0; i < 15; i++) {
                float y = 340 * density + i * 36 * density;
                Path wave = new Path(); wave.moveTo(-40, y);
                wave.cubicTo(w * 0.3f, y - 38 * density, w * 0.6f, y + 35 * density, w + 40, y - 10 * density);
                paint.setColor(i % 3 == 0 ? 0x225DD8C1 : 0x116BCABC); canvas.drawPath(wave, paint);
            }
            float x = w * 0.76f, y = 470 * density, s = Math.min(w * 0.19f, 82 * density);
            paint.setStyle(Paint.Style.FILL); paint.setColor(0x99E8C97E);
            Path sail = new Path(); sail.moveTo(x, y - s); sail.lineTo(x - s * 0.72f, y - 8 * density); sail.lineTo(x, y - 8 * density); sail.close(); canvas.drawPath(sail, paint);
            paint.setColor(0x55F6F1DB); Path other = new Path(); other.moveTo(x + 4 * density, y - s * 0.82f); other.lineTo(x + s * 0.55f, y - 8 * density); other.lineTo(x + 4 * density, y - 8 * density); other.close(); canvas.drawPath(other, paint);
            paint.setColor(0x99E8C97E); Path hull = new Path(); hull.moveTo(x - s * 0.78f, y); hull.lineTo(x + s * 0.75f, y); hull.lineTo(x + s * 0.38f, y + 12 * density); hull.lineTo(x - s * 0.48f, y + 12 * density); hull.close(); canvas.drawPath(hull, paint);
        }
    }
}
