/* 开发期预览：在不启动 QEMU 的情况下把界面渲染成 BMP，用于检查版式。
 * 不参与系统构建，仅主机侧使用：
 *   gcc -O2 -Isrc/gfx tools/preview.c src/gfx/gfx.c src/gfx/ui.c -lm -o /tmp/preview
 *   /tmp/preview /tmp/preview.bmp
 */
#include "gfx.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

static gfx_t G;
static ui_t U;

static void fake_screen(int W, int H)
{
    gfx_offscreen(&G, W, H);
    ui_init(&U, &G);
}

static void demo_installer(int W, int H)
{
    fake_screen(W, H);
    int pad = 40;

    /* 顶部品牌区 */
    gfx_fill(&G, 0, 0, W, H, C_BG);
    gfx_fill(&G, 0, 0, W, 300, 0x1B2A4A);
    gfx_vgrad(&G, 0, 0, W, 300, 0x24406E, 0x16233C);

    gfx_round(&G, pad, 52, 64, 64, 20, C_PRIMARY);
    gfx_icon(&G, "chip", pad + 12, 64, 40, C_WHITE);

    gfx_text(&G, pad + 84, 58, "ProperOS 2.5", C_WHITE, 2);
    gfx_text(&G, pad + 84, 58 + gfx_text_h(2) + 4, "Mobile · ARM64 安装器", 0xA8BCE0, 1);

    gfx_text(&G, pad, 168, "选择安装位置", C_WHITE, 2);
    gfx_text(&G, pad, 168 + gfx_text_h(2) + 10, "安装会清除目标磁盘上的所有数据", 0x9FB3D6, 1);

    /* 卡片列表 */
    int cy = 306, ch = 292;
    ui_card(&U, pad, cy, W - pad * 2, ch, 26);

    ui_item_t items[3] = {
        { "/dev/vda", "64 GB · VirtIO Block Device", "chip", C_PRIMARY },
        { "/dev/mmcblk0", "32 GB · SD Card", "chip", 0x8B5CF6 },
        { "/dev/sda", "16 GB · USB Storage", "chip", 0xF59E0B },
    };
    ui_list(&U, pad + 12, cy + 14, W - pad * 2 - 24, ch - 28, items, 3, 0);

    /* 说明 */
    int ty = cy + ch + 34;
    gfx_icon(&G, "shield", pad, ty, 22, C_WARN);
    gfx_text(&G, pad + 32, ty + 2, "分区方案：ESP 256 MiB · 系统 4 GiB · 数据为剩余空间", C_TEXT_2, 1);

    /* 底部按钮 */
    int bw = (W - pad * 2 - 16) / 2;
    ui_button(&U, pad, H - 130, bw, 64, "上一步", BTN_SECONDARY);
    ui_button(&U, pad + bw + 16, H - 130, bw, 64, "继续", BTN_PRIMARY);

    gfx_flush(&G);
    gfx_save_bmp(&G, "/tmp/preview-installer.bmp");
    gfx_close(&G);
}

static void demo_desktop(int W, int H)
{
    fake_screen(W, H);
    gfx_vgrad(&G, 0, 0, W, H, 0xF7F9FE, 0xEEF2FB);

    /* 顶部圆角工具卡 */
    int pad = 22;
    int card_h = 150;
    ui_card(&U, pad, pad, W - pad * 2, card_h, 30);
    gfx_text(&G, pad + 26, pad + 24, "21:04", C_TEXT, 2);
    gfx_text(&G, pad + 26, pad + 24 + gfx_text_h(2) + 6, "11月25日 周二 · 晚上好", C_TEXT_3, 1);

    gfx_icon(&G, "wifi", W - pad - 112, pad + 40, 24, C_TEXT_2);
    gfx_icon(&G, "battery", W - pad - 76, pad + 40, 26, C_TEXT_2);
    gfx_icon(&G, "bluetooth", W - pad - 40, pad + 40, 24, C_TEXT_2);

    /* 搜索条 */
    int sy = pad + card_h + 16;
    gfx_round(&G, pad, sy, W - pad * 2, 52, 16, 0xFFFFFF);
    gfx_icon(&G, "search", pad + 16, sy + 15, 22, C_TEXT_3);
    gfx_text(&G, pad + 48, sy + 15, "搜索应用", C_TEXT_3, 1);

    /* 图标网格 */
    const char *ids[] = { "phone", "message", "browser", "camera",
                          "settings", "files", "terminal", "music",
                          "photos", "clock", "weather", "maps",
                          "mail", "calc", "notes", "store" };
    const char *names[] = { "电话", "信息", "浏览器", "相机",
                            "设置", "文件", "终端", "音乐",
                            "相册", "时钟", "天气", "地图",
                            "邮件", "计算器", "笔记", "应用商店" };
    uint32_t cols[] = { 0x34C759, 0x30D158, 0x3E7BFF, 0x8E8E93,
                        0x636366, 0xF5A623, 0x1C1C1E, 0xE5484D,
                        0xFF9500, 0x5856D6, 0x32ADE6, 0x34C759,
                        0x007AFF, 0xFF9F0A, 0xFFD60A, 0x3E7BFF };

    int cols_n = 4;
    int gx = pad;
    int gy = sy + 92;
    int cell = (W - pad * 2) / cols_n;
    int icon = cell * 58 / 100;
    if (icon > 100) icon = 100;
    for (int i = 0; i < 16; i++) {
        int r = i / cols_n, c = i % cols_n;
        int x = gx + c * cell + (cell - icon) / 2;
        int y = gy + r * (cell + 12);
        gfx_round(&G, x, y, icon, icon, icon / 4, cols[i]);
        gfx_icon(&G, ids[i], x + icon / 4, y + icon / 4, icon / 2, C_WHITE);
        gfx_text_center(&G, x + icon / 2, y + icon + 10, names[i], C_TEXT, 1);
    }

    /* 底部 Dock */
    int dock_w = W - pad * 2 - 40;
    int dock_h = 96;
    int dock_y = H - dock_h - 30;
    gfx_shadow(&G, pad + 20, dock_y, dock_w, dock_h, 28, 16, 30);
    gfx_round(&G, pad + 20, dock_y, dock_w, dock_h, 28, 0xF2F5FC);
    gfx_round_outline(&G, pad + 20, dock_y, dock_w, dock_h, 28, 1, 0xE3E9F5);
    const char *dock_ids[] = { "phone", "message", "browser", "camera" };
    uint32_t dock_col[] = { 0x34C759, 0x30D158, 0x3E7BFF, 0x8E8E93 };
    for (int i = 0; i < 4; i++) {
        int ic = 54;
        int x = pad + 20 + (dock_w - 4 * (ic + 34)) / 2 + i * (ic + 34) + 17;
        gfx_round(&G, x, dock_y + (dock_h - ic) / 2, ic, ic, 16, dock_col[i]);
        gfx_icon(&G, dock_ids[i], x + 13, dock_y + (dock_h - ic) / 2 + 13, 28, C_WHITE);
    }

    gfx_flush(&G);
    gfx_save_bmp(&G, "/tmp/preview-desktop.bmp");
    gfx_close(&G);
}

int main(int argc, char **argv)
{
    demo_installer(720, 1280);
    demo_desktop(720, 1280);
    printf("written /tmp/preview-installer.bmp /tmp/preview-desktop.bmp\n");
    (void)argc; (void)argv;
    return 0;
}
