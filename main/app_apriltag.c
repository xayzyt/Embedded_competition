/*
 * app_apriltag.c - AprilTag Tag36h11 轻量识别模块
 *
 * 这个文件负责在 ESP32-P4 本地对灰度图进行 AprilTag 识别。它没有依赖大型视觉库，
 * 而是自己完成二值化、候选区域连通域搜索、四边形校验、透视采样、bit 解码、码表匹配等流程。
 *
 * 在你的 SkyAnchor 项目里，它处在“视觉底层算法”位置：
 * - app_camera.c 负责采集摄像头画面；
 * - app_vision.c 把 RGB565 画面转成灰度图，并调用本文件的 app_apriltag_detect_tag36h11()；
 * - app_dock_judge.c 再根据识别结果判断无人机 ID、距离和稳定性是否满足接驳条件。
 *
 * 注意：文件中 s_tag36h11_codes[] 是 Tag36h11 官方码族的查表数据，属于纯数据表，
 * 不需要逐个常量解释；真正需要理解的是“从图像找候选框 -> 采样网格 -> 计算汉明距离 -> 输出 tag 结果”的流程。
 */

#include "app_apriltag.h"
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#define AT_MAX_WIDTH            360
#define AT_MAX_HEIGHT           280
#define AT_MAX_PIXELS           (AT_MAX_WIDTH * AT_MAX_HEIGHT)
#define AT_MIN_AREA             160
#define AT_MIN_SIDE             20
#define AT_MAX_CANDIDATES       10
#define AT_MAX_HAMMING          4
#define AT_GRID_SIZE            8
#define AT_DATA_GRID            6
#define AT_RING_CELLS           28
#define AT_EPS                  1e-6f
#define AT_EDGE_MARGIN          1U
#define AT_MAX_BOX_PCT          78U
#define AT_MAX_BOX_AREA_PCT     72U
static const char *TAG = "app_apriltag";
static uint8_t *s_binary = NULL;
static uint8_t *s_visited = NULL;
static uint32_t *s_queue = NULL;
static bool s_inited = false;

/* -------------------------------------------------------------------------- */
/* Tag36h11 参考码表                                                     */
/* -------------------------------------------------------------------------- */

/*
 * Tag36h11 合法码表。检测时会把采样出的 6x6 数据位打包成编码，然后遍历这个表寻找汉明距离最近的 ID。这里是纯数据，不建议逐个常量解释，否则反而影响阅读。
 */
static const uint64_t s_tag36h11_codes[587] = {
    0x21a146babULL,
    0x92d18fe9bULL,
    0x7089014bbULL,
    0x193979e27ULL,
    0x44153d3d7ULL,
    0x35cd5b8cfULL,
    0xa10ba56a0ULL,
    0x2b874a608ULL,
    0xb57fb8d44ULL,
    0x4e20b5a64ULL,
    0x61d897f2cULL,
    0xab3469ffcULL,
    0x594ca45c2ULL,
    0xfa1c2d2e2ULL,
    0x97c24b972ULL,
    0x75928624aULL,
    0x1caafe99aULL,
    0x3236ddc16ULL,
    0x884e527d6ULL,
    0x5771a8c7eULL,
    0xf52925b81ULL,
    0x2e59cc1a1ULL,
    0x7fa58ab31ULL,
    0xeb43384f9ULL,
    0x3913d5345ULL,
    0xd79b1a3b5ULL,
    0x0dfbe768dULL,
    0x5cd7a031dULL,
    0xb9c8f7a4bULL,
    0x46987e06bULL,
    0x6e0c5d527ULL,
    0x5bc567bb4ULL,
    0x1e6302bc2ULL,
    0x8ac7d046aULL,
    0xa65fe2326ULL,
    0x5400eb616ULL,
    0xf7a066836ULL,
    0x2dd0afdf6ULL,
    0x3b9cb60b1ULL,
    0xad6a1caf9ULL,
    0x93b62efb5ULL,
    0xbb210d93dULL,
    0xdee55094bULL,
    0xa0cd1479bULL,
    0x2a23abb27ULL,
    0x4f67f4b5fULL,
    0x58a815818ULL,
    0x743427d64ULL,
    0x1dec40874ULL,
    0x892ab3a02ULL,
    0x38d5742eeULL,
    0x0c43567a1ULL,
    0x5db311cb1ULL,
    0xc957ca1f9ULL,
    0xa22457823ULL,
    0x29ace98f3ULL,
    0xccc52c528ULL,
    0xda8b3578cULL,
    0xd5b4b9e36ULL,
    0xac9cdd3ceULL,
    0xcbdaa8391ULL,
    0x1986256b1ULL,
    0xbae6cc889ULL,
    0xdf91b10e5ULL,
    0xa1a9d5a75ULL,
    0x2b651aa9dULL,
    0x4d5c2e19dULL,
    0x5b163736bULL,
    0xf753d60a8ULL,
    0x5e84b37d2ULL,
    0xfce43a9f2ULL,
    0xbb926d1a6ULL,
    0xed8d236e5ULL,
    0x6af846f37ULL,
    0xea56a5138ULL,
    0x5d497146cULL,
    0x470360ecaULL,
    0x8c4bcd67aULL,
    0x295f3901eULL,
    0xf6b577e97ULL,
    0x520602f5aULL,
    0xb5119492eULL,
    0x48e299ab7ULL,
    0xb79f5737aULL,
    0xa814c683eULL,
    0x2da900583ULL,
    0xd4aba4797ULL,
    0x982a0e65cULL,
    0xf480ccc15ULL,
    0x0b20ea732ULL,
    0xa9506710aULL,
    0x0486e2ee1ULL,
    0xb0a972615ULL,
    0xe73f3a257ULL,
    0x17e205b98ULL,
    0xec9ab2b24ULL,
    0xd9339e076ULL,
    0xeb8874f99ULL,
    0x9420a55ccULL,
    0xe364ff992ULL,
    0x8a4cb8f0aULL,
    0xf72aee766ULL,
    0x73cc6b569ULL,
    0xc99ca6a59ULL,
    0x366a0c535ULL,
    0x509e51943ULL,
    0xba6d227d1ULL,
    0xd2acf163bULL,
    0xd989fef68ULL,
    0x641707bb2ULL,
    0xf9bd312abULL,
    0x77739c207ULL,
    0x68bf8d1bfULL,
    0xdedd84a6eULL,
    0x42eaec49bULL,
    0x302b68d53ULL,
    0x4ebb930ceULL,
    0x439c5d9b5ULL,
    0xe1fc9228dULL,
    0xf8a6a8d4aULL,
    0xb53902a18ULL,
    0xb163475eeULL,
    0xad2f5af29ULL,
    0xaab5dc850ULL,
    0xb3ec25786ULL,
    0x4799c4405ULL,
    0x8bc68dc04ULL,
    0x702f15ddeULL,
    0xb42293da9ULL,
    0x1acad0382ULL,
    0x813363971ULL,
    0x0e692ad70ULL,
    0x8ad05c5b9ULL,
    0x85e654c1bULL,
    0x3d0ac2b3bULL,
    0x4bbfa0552ULL,
    0x722dde306ULL,
    0x369d1acd2ULL,
    0xa2d994e60ULL,
    0x5fad9139dULL,
    0x57535ae96ULL,
    0x5f5ef1d46ULL,
    0xebc3e6accULL,
    0x0a99c835dULL,
    0x5463e49a5ULL,
    0x25676893aULL,
    0xabed37064ULL,
    0x59bdded94ULL,
    0x64fa04c09ULL,
    0x250e44e75ULL,
    0xe58a374bcULL,
    0x24caa5741ULL,
    0xe42cf6588ULL,
    0x6ea238538ULL,
    0xf75813025ULL,
    0x0ceb33bbdULL,
    0x786ae8670ULL,
    0xf0de8c969ULL,
    0x26c81efe6ULL,
    0x69f489086ULL,
    0x3fbf92a4dULL,
    0xd6555a760ULL,
    0x9ccae236dULL,
    0xb256c16a3ULL,
    0xd78ac7221ULL,
    0x2dc1fe262ULL,
    0xaf2051090ULL,
    0x932c08afeULL,
    0x53feca775ULL,
    0x5d292497aULL,
    0x2f101a22bULL,
    0xc6ad16b14ULL,
    0xd2eca4b20ULL,
    0xa6fa603d4ULL,
    0x3057c3b69ULL,
    0xe817b54d2ULL,
    0x40abfc3e0ULL,
    0xf53ad16e7ULL,
    0x36f3a06e8ULL,
    0x27782e418ULL,
    0x3e9062146ULL,
    0x01fd7ad4eULL,
    0xc8765bc64ULL,
    0xad3d8215fULL,
    0x96317a27eULL,
    0x05f813ca6ULL,
    0x0e7adc22eULL,
    0xbf52c4a50ULL,
    0x724f6ae2eULL,
    0x540fd7ea8ULL,
    0x9c962f741ULL,
    0x264042b01ULL,
    0x197cb7754ULL,
    0x1f2bb850dULL,
    0xc87d6c2adULL,
    0xdc8d2dedeULL,
    0xcb6b01738ULL,
    0xbe4210368ULL,
    0xbb4622a77ULL,
    0x733226ef8ULL,
    0x4933ade39ULL,
    0x1f1c896c7ULL,
    0x897665abaULL,
    0x92f46ba12ULL,
    0x297981767ULL,
    0xad702cf8dULL,
    0x473198b90ULL,
    0xec2e8e06bULL,
    0x36a1d6980ULL,
    0xa5ce8650fULL,
    0x637870348ULL,
    0xf3ab41d46ULL,
    0xe6032a8b5ULL,
    0x4c11eec73ULL,
    0x261de50d6ULL,
    0x296616e32ULL,
    0x37cff1a0aULL,
    0xc457847abULL,
    0x776e52ff9ULL,
    0x980819bb9ULL,
    0x50cb2b5d8ULL,
    0x8b6918063ULL,
    0x12d9c2ce9ULL,
    0x158c2e738ULL,
    0x6b84015eaULL,
    0x67695db9fULL,
    0x3e72745c1ULL,
    0x01ca1a571ULL,
    0x22e7036f5ULL,
    0xd5dcedae1ULL,
    0x809532c5bULL,
    0xf7e4e9d54ULL,
    0x59d84f9cbULL,
    0xf42ed0851ULL,
    0x5db4a91faULL,
    0x0d58e5b1fULL,
    0x69fb9fa43ULL,
    0x29ba51f60ULL,
    0x587c67427ULL,
    0xd8b9021edULL,
    0xbb594449eULL,
    0xaa231f35aULL,
    0x0d95943f4ULL,
    0xdec461257ULL,
    0x05a16930dULL,
    0xe2512eb58ULL,
    0xab48e2dc0ULL,
    0x0cb68494fULL,
    0x927a6b5b1ULL,
    0xbb90f3450ULL,
    0x632e8fad0ULL,
    0xc2eeaf2a7ULL,
    0x1eb2af39dULL,
    0x511548c52ULL,
    0x4693d5de4ULL,
    0x888e9161dULL,
    0x68836ff4fULL,
    0x51bd2e441ULL,
    0x7a0ac40e9ULL,
    0x8bb762b4aULL,
    0x1caf83a1aULL,
    0xc2700bbbdULL,
    0xfb2ed09aaULL,
    0xf8520294eULL,
    0x7aa104ca6ULL,
    0x6a96d2bcbULL,
    0x28f2ec75eULL,
    0x607065eb4ULL,
    0x7ad9b3a8cULL,
    0x3572d6cbfULL,
    0xd426efb8fULL,
    0x64ee4dbaaULL,
    0x983d7a8f1ULL,
    0x8ca31f499ULL,
    0x025457b46ULL,
    0xd53ae4149ULL,
    0x9d3c64396ULL,
    0x795332ee5ULL,
    0x70884d664ULL,
    0x1dd3528d8ULL,
    0xd8e51d246ULL,
    0xd6029ca38ULL,
    0xd94ab610cULL,
    0x766bb8880ULL,
    0xfd0c6b319ULL,
    0x3bff8b9d1ULL,
    0xe786f4d4eULL,
    0xd4e6b9b79ULL,
    0xcfa8bb369ULL,
    0x20a4366c6ULL,
    0x5cefb9536ULL,
    0x0f06b6605ULL,
    0xbb51d25e7ULL,
    0xf7e262641ULL,
    0x81fda9855ULL,
    0x2241dbc59ULL,
    0x56fb4b280ULL,
    0xeda64c6d0ULL,
    0xce7b080f4ULL,
    0x6192ee7e9ULL,
    0x943ff5124ULL,
    0x88b3beadfULL,
    0x9b27e58d0ULL,
    0xbe0b45fd2ULL,
    0x69fb3a72aULL,
    0x61383a832ULL,
    0x6688639b3ULL,
    0x54d01c992ULL,
    0x90b10c02aULL,
    0xf667fc6d1ULL,
    0x7acd75813ULL,
    0x3abd07f61ULL,
    0x543012473ULL,
    0x067794d58ULL,
    0xa855b1201ULL,
    0x9cb4d7fdbULL,
    0x31a193e6aULL,
    0x3da3b76d4ULL,
    0x4153f1e04ULL,
    0x5544335b5ULL,
    0x92f61249dULL,
    0x269a35f8aULL,
    0x042677896ULL,
    0x9487cdc6fULL,
    0x4ea7e7d9cULL,
    0x2e367345bULL,
    0x3a5e8f304ULL,
    0x76d9faa77ULL,
    0x64e4ea1dfULL,
    0x12a7b434dULL,
    0x4973d9553ULL,
    0xcdb418975ULL,
    0x8bda76d2eULL,
    0xf54a4dec8ULL,
    0x811616f8aULL,
    0x4036f4e6bULL,
    0x2736282a6ULL,
    0xa413d229fULL,
    0x901fe4c17ULL,
    0x9abfce56eULL,
    0x2d325b9efULL,
    0xd9901f6a9ULL,
    0x3aa64fadeULL,
    0x65288d60bULL,
    0x0a53e960bULL,
    0xe9e09c3e4ULL,
    0x4d9b7863dULL,
    0xd98bb4833ULL,
    0x34b181ed7ULL,
    0x5b373f490ULL,
    0x8572b7b91ULL,
    0x2c5d056a4ULL,
    0xe4671296dULL,
    0x2a08d6f78ULL,
    0x452949c61ULL,
    0x2501ec5b7ULL,
    0xb8f54779cULL,
    0xa3b7ea4b3ULL,
    0x443d6b9b4ULL,
    0xbe2dcb848ULL,
    0xb51fc47ecULL,
    0x6f38cf988ULL,
    0x90f2fe934ULL,
    0x07fd540abULL,
    0xee53c3d08ULL,
    0xf7098ebcaULL,
    0x65467c309ULL,
    0xae6fd797eULL,
    0x8bc4739e9ULL,
    0x971930fe4ULL,
    0x9a752df57ULL,
    0xbd47fe53eULL,
    0x6f1530928ULL,
    0x5de1cc489ULL,
    0x111ce3dcdULL,
    0xa4c7e157cULL,
    0x552691112ULL,
    0x9d6511805ULL,
    0x6663c1cb3ULL,
    0xbfc594002ULL,
    0x3a3500387ULL,
    0x76d4f679cULL,
    0x446f2667bULL,
    0x17ae59e97ULL,
    0x36132d63eULL,
    0x8e07492bfULL,
    0x188acddf1ULL,
    0xed9052d02ULL,
    0x5b6bd01f5ULL,
    0x84a4d070eULL,
    0xc9b2a3af4ULL,
    0xab18852fcULL,
    0x9de918390ULL,
    0x3576ee9e0ULL,
    0xe8d00d210ULL,
    0x709b5a95cULL,
    0xa9022a314ULL,
    0xa03ea2b9cULL,
    0x1e589ec5eULL,
    0x74470eeddULL,
    0x36bd44159ULL,
    0xb7f8d2516ULL,
    0xdcde4bfa5ULL,
    0x9eda99c30ULL,
    0x8ffb98a8bULL,
    0x9ac777e71ULL,
    0x8d0a53cb3ULL,
    0xc9ed1dcedULL,
    0xc238535eaULL,
    0xee4aa6437ULL,
    0xcd8345a1eULL,
    0x5a19e05aeULL,
    0x96d780d25ULL,
    0xe2c885185ULL,
    0xc5314f8dbULL,
    0xa47c37ee1ULL,
    0x1d82c6f8cULL,
    0x325c5c6c8ULL,
    0x5d9d1bae0ULL,
    0x9549e075eULL,
    0x8a6845c15ULL,
    0xadf9f10feULL,
    0x95cef30b0ULL,
    0x71933b20dULL,
    0x1f0de3a6fULL,
    0x596f5bba6ULL,
    0xcd61d9f34ULL,
    0xa478ad328ULL,
    0xdade4012fULL,
    0xd15764370ULL,
    0x3a2bf5d22ULL,
    0x2303e348dULL,
    0x41b9bf9a9ULL,
    0x5a4e93ee3ULL,
    0x462b44512ULL,
    0x4e01719c1ULL,
    0x73a0d9137ULL,
    0x932b416ddULL,
    0xff4b615a7ULL,
    0x2c880f1acULL,
    0x6f3bb6aa1ULL,
    0x476d21ae6ULL,
    0xf9c290688ULL,
    0x768111210ULL,
    0x623458f55ULL,
    0xf31919e1fULL,
    0xef47a7dd1ULL,
    0xc6c777808ULL,
    0xeec96f096ULL,
    0x9b3c9bdafULL,
    0x6fd432573ULL,
    0x60e7d0aa9ULL,
    0x0651cda34ULL,
    0xe2acc04e7ULL,
    0x9460d1a22ULL,
    0x82b79b177ULL,
    0x4eb189f4bULL,
    0xcbf9c3b1eULL,
    0x6f6c64841ULL,
    0xe54e50a2eULL,
    0x632f30f23ULL,
    0xb3bb3de6cULL,
    0x6331f5383ULL,
    0xbca1dafe5ULL,
    0xd1816cab9ULL,
    0x0d8407acaULL,
    0x794e1d7e5ULL,
    0x92de65442ULL,
    0x651497949ULL,
    0x33f751712ULL,
    0x3c9ce9195ULL,
    0x7ecb81d7aULL,
    0xede140178ULL,
    0xc09b0ee2cULL,
    0x14e61f774ULL,
    0x6d336649aULL,
    0x865dd0880ULL,
    0xf7214bfb8ULL,
    0xd8b9c2e22ULL,
    0x99a70737fULL,
    0xc0ecf90c9ULL,
    0xbb8b26e01ULL,
    0x05678fcacULL,
    0x33b483834ULL,
    0xeb9d84b83ULL,
    0x7ca277ea2ULL,
    0x02af5f66bULL,
    0x4a678c122ULL,
    0x121592bf3ULL,
    0xcc82b48c9ULL,
    0x77cbe90fcULL,
    0x5ef7d7931ULL,
    0x117947995ULL,
    0x344d5f970ULL,
    0xdf2f12e72ULL,
    0x114277b37ULL,
    0x0b8fa73d5ULL,
    0x36e16f423ULL,
    0x057402b7eULL,
    0xc342423a7ULL,
    0x758c34dabULL,
    0x67dc61614ULL,
    0x29b222068ULL,
    0x7101af0faULL,
    0x54d22bc75ULL,
    0x908ba3af9ULL,
    0xeb68c9b51ULL,
    0x2605d442cULL,
    0x6ab88b7f4ULL,
    0x207c7657dULL,
    0x377940d20ULL,
    0xac2eb77b6ULL,
    0x3e87318ecULL,
    0x47afea1aaULL,
    0x46de35b74ULL,
    0x48a36ba10ULL,
    0xf0711536bULL,
    0x963f87ed4ULL,
    0xd83ab9965ULL,
    0x7b2eb5a0fULL,
    0xb6adfaf32ULL,
    0x87e07a4eaULL,
    0x7b4518f4eULL,
    0x9064a3738ULL,
    0x3c242ca12ULL,
    0x96b25c4a4ULL,
    0x8b01d83acULL,
    0xe51cceb25ULL,
    0x480f58db0ULL,
    0xbae295867ULL,
    0x89fc0fba0ULL,
    0xdd06475e9ULL,
    0xf7fe46b4cULL,
    0x43cef026dULL,
    0x6393585a2ULL,
    0x87b404456ULL,
    0x4fa47cbeaULL,
    0x7619f955aULL,
    0x51b9a1330ULL,
    0x1533637c7ULL,
    0x7587a972aULL,
    0x60de11622ULL,
    0x7d4ce68aeULL,
    0xaf135e81cULL,
    0x31c9b647cULL,
    0xac43d8155ULL,
    0xd853e1cfbULL,
    0x4fcdc5cf0ULL,
    0xee696fd2dULL,
    0xb71618475ULL,
    0xb9b4b8781ULL,
    0x328579505ULL,
    0x95e947771ULL,
    0x9aa0302e2ULL,
    0xec1828d17ULL,
    0xbc5079635ULL,
    0x8785c1b79ULL,
    0xd37aadb7aULL,
    0x817f02ce0ULL,
    0xfd63dfb0aULL,
    0x510ac1af2ULL,
    0x3d1edf8d3ULL,
    0xf4b73a402ULL,
    0x37d789e9cULL,
    0x8f11c77caULL,
    0xc4bd036beULL,
    0x7dfa995c9ULL,
    0x7b14e8251ULL,
    0xca070e7f4ULL,
    0x5c22dbcd5ULL,
    0xa0db726c8ULL,
    0x6b7776de2ULL,
    0x80f58e748ULL,
    0xe1ac1ff4aULL,
    0x1bbaea85cULL,
    0x26ac557fcULL,
    0xe5d1ef357ULL,
    0x1f29d4e10ULL,
    0x459fbb15bULL,
    0xd4b049559ULL,
    0x6cb70acf8ULL,
    0xf8617a518ULL,
    0xc391dac34ULL,
    0x51235e2ccULL,
    0x74407c4feULL,
    0xdf1609a24ULL,
    0xced27dc17ULL,
};
static const uint8_t s_tag36h11_bit_x[36] = {
    1,2,3,4,5,2,3,4,3,6,6,6,6,6,5,5,5,4,6,5,4,3,2,5,4,3,4,1,1,1,1,1,2,2,2,3
};
static const uint8_t s_tag36h11_bit_y[36] = {
    1,1,1,1,1,2,2,2,3,1,2,3,4,5,2,3,4,3,6,6,6,6,6,5,5,5,4,6,5,4,3,2,5,4,3,4
};

/* -------------------------------------------------------------------------- */
/* 内部几何和打包类型                                         */
/* -------------------------------------------------------------------------- */

/*
 * 结构体类型：把同一类运行时数据或协议字段打包在一起，方便函数之间传递。
 */
typedef struct {
    float x;    /* 图像坐标系中的 x 坐标，单位为像素。 */
    float y;    /* 图像坐标系中的 y 坐标，单位为像素。 */
} at_pt_t;
/*
 * 结构体类型：把同一类运行时数据或协议字段打包在一起，方便函数之间传递。
 */
typedef struct {
    bool valid;        /* 候选区域是否通过基础筛选。 */
    uint32_t area;     /* 连通域像素面积。 */
    uint32_t min_x;    /* 候选外接框左边界。 */
    uint32_t min_y;    /* 候选外接框上边界。 */
    uint32_t max_x;    /* 候选外接框右边界。 */
    uint32_t max_y;    /* 候选外接框下边界。 */
    float cx;          /* 候选区域质心 x 坐标。 */
    float cy;          /* 候选区域质心 y 坐标。 */
    at_pt_t tl;        /* 近似左上角点。 */
    at_pt_t tr;        /* 近似右上角点。 */
    at_pt_t br;        /* 近似右下角点。 */
    at_pt_t bl;        /* 近似左下角点。 */
    uint32_t score;    /* 候选质量分数，用于保留最值得解码的区域。 */
} at_candidate_t;
/*
 * 枚举类型：用一组有名字的常量表示状态/类型，比直接写数字更清晰，也方便调试。
 */
typedef enum {
    AT_BITS_IDENTITY = 0,
    AT_BITS_MIRROR_X,
    AT_BITS_MIRROR_Y,
    AT_BITS_TRANSPOSE,
    AT_BITS_TRANSPOSE_MIRROR_X,
    AT_BITS_TRANSPOSE_MIRROR_Y,
    AT_BITS_TRANSFORM_COUNT,
} at_bits_transform_t;
/*
 * 枚举类型：用一组有名字的常量表示状态/类型，比直接写数字更清晰，也方便调试。
 */
typedef enum {
    AT_PACK_FAMILY_MSB = 0,
    AT_PACK_FAMILY_LSB,
    AT_PACK_ROW_MSB,
    AT_PACK_ROW_LSB,
    AT_PACK_COL_MSB,
    AT_PACK_COL_LSB,
    AT_PACK_MODE_COUNT,
} at_pack_mode_t;

/* -------------------------------------------------------------------------- */
/* 图像阈值化和候选收集                                 */
/* -------------------------------------------------------------------------- */

/*
 * 把二维图像坐标 (x,y) 转成一维数组下标，灰度图/二值图都按行优先连续存储。
 */
static inline uint32_t at_index(uint32_t x, uint32_t y, uint32_t width)
{
    return y * width + x;
}
/*
 * 使用 Otsu 大津法自动计算灰度阈值，让二值化不依赖固定光照条件。
 */
static uint8_t at_otsu_threshold(const uint8_t *gray, uint32_t width, uint32_t height)
{
    /*
     * 统计 0~255 灰度直方图，同时计算整幅图灰度总和。
     * Otsu 后面会用这些值快速计算前景/背景类间方差。
     */
    uint32_t hist[256] = {0};
    const uint32_t total = width * height;
    uint64_t sum = 0;
    for (uint32_t i = 0; i < total; i++) {
        uint8_t v = gray[i];
        hist[v]++;
        sum += v;
    }

    /*
     * 遍历所有可能阈值，寻找类间方差最大的阈值。
     * 这里用整数形式计算，避免在 ESP32 上频繁做浮点除法。
     */
    uint64_t sum_b = 0;
    uint32_t w_b = 0;
    uint64_t best_between = 0;
    uint8_t best_t = 127;
    for (uint32_t t = 0; t < 256; t++) {
        w_b += hist[t];
        if (w_b == 0) continue;
        uint32_t w_f = total - w_b;
        if (w_f == 0) break;
        sum_b += (uint64_t)t * hist[t];
        int64_t diff = (int64_t)(sum_b * w_f) - (int64_t)((sum - sum_b) * w_b);
        uint64_t between = (uint64_t)(diff * diff);
        if (between > best_between)
        {
            best_between = between;
            best_t = (uint8_t)t;
        }
    }
    return best_t;
}
/*
 * 计算两个 64 位编码之间的汉明距离，用于判断采样出的 tag code 最接近哪个合法 ID。
 */
static inline uint32_t at_hamming64(uint64_t a, uint64_t b)
{
    uint64_t x = a ^ b;
#if defined(__GNUC__)
    return (uint32_t)__builtin_popcountll(x);
#else
    uint32_t c = 0;
    while (x)
   { x &= (x - 1); c++; }
    return c;
#endif
}
/*
 * 根据阈值把灰度图转成黑白二值图，后续连通域搜索只在二值图上进行。
 */
static void at_threshold_binary(const uint8_t *gray, uint32_t width, uint32_t height, uint8_t threshold)
{
    const uint32_t total = width * height;
    for (uint32_t i = 0; i < total; i++) {
        s_binary[i] = (gray[i] < threshold) ? 1U : 0U;
        s_visited[i] = 0U;
    }
}
/*
 * 根据连通域面积、宽高、比例等条件过滤明显不像 AprilTag 的区域。
 */
static bool at_filter_component(uint32_t area,
                                uint32_t min_x,
                                uint32_t min_y,
                                uint32_t max_x,
                                uint32_t max_y,
                                uint32_t *score_out)
{
    /*
     * 连通域外接矩形宽高。
     */
    uint32_t w = max_x - min_x + 1U;
    uint32_t h = max_y - min_y + 1U;

    /*
     * 面积或边长太小的区域大概率是噪点、文字或图像纹理，不继续解码。
     */
    if (area < AT_MIN_AREA || w < AT_MIN_SIDE || h < AT_MIN_SIDE) return false;

    /*
     * AprilTag 外框接近正方形。
     * 如果长宽比例差距太大，通常不是有效 tag。
     */
    uint32_t min_side = (w < h) ? w : h;
    uint32_t max_side = (w > h) ? w : h;
    if (min_side * 100U < max_side * 55U) return false;

    /*
     * 填充率过滤。
     * 太稀疏可能是细线/噪点，太满可能是大块阴影。
     */
    uint32_t box_area = w * h;
    uint32_t fill_pct = (area * 100U) / box_area;
    if (fill_pct < 12U || fill_pct > 92U) return false;

    /*
     * 候选评分：面积越大、越接近正方形越优先。
     * 后续只保留前 AT_MAX_CANDIDATES 个候选，减少透视解码耗时。
     */
    uint32_t score = area;
    score += min_side * 6U;
    score -= (max_side - min_side) * 5U;
    *score_out = score;
    return true;
}
/*
 * 判断候选框是否贴到图像边缘；贴边目标通常不完整，容易误判。
 */
static bool at_candidate_touch_edge(const at_candidate_t *cand, uint32_t width, uint32_t height, uint32_t margin)
{
    if (cand == NULL || width == 0U || height == 0U) return true;
    if (cand->min_x <= margin || cand->min_y <= margin) return true;
    if (cand->max_x + margin >= width - 1U) return true;
    if (cand->max_y + margin >= height - 1U) return true;
    return false;
}
/*
 * 过滤过大的候选框，避免整片阴影/屏幕边缘被误认为 tag。
 */
static bool at_candidate_too_large(const at_candidate_t *cand, uint32_t width, uint32_t height)
{
    if (cand == NULL || width == 0U || height == 0U) return true;
    uint32_t bw = cand->max_x - cand->min_x + 1U;
    uint32_t bh = cand->max_y - cand->min_y + 1U;
    if ((bw * 100U) >= (width * AT_MAX_BOX_PCT)) return true;
    if ((bh * 100U) >= (height * AT_MAX_BOX_PCT)) return true;
    uint32_t box_area = bw * bh;
    if ((box_area * 100U) >= (width * height * AT_MAX_BOX_AREA_PCT)) return true;
    return false;
}
/*
 * 把新的候选区域按面积/质量插入候选列表，只保留最值得继续解码的前几个候选。
 */
static void at_insert_candidate(at_candidate_t *cands, int *count, const at_candidate_t *cand)
{
    if (!cand->valid) return;
    int n = *count;
    if (n < AT_MAX_CANDIDATES)
    {
        cands[n] = *cand;
        (*count)++;
    }
    else
    {
        int worst = 0;
        for (int i = 1; i < n; i++) {
            if (cands[i].score < cands[worst].score) worst = i;
        }
        if (cand->score > cands[worst].score) cands[worst] = *cand;
    }
}
/*
 * 在二值图上做连通域搜索，找到可能是 AprilTag 黑色外框的候选区域。
 */
static int at_collect_candidates(uint32_t width, uint32_t height, at_candidate_t *cands)
{
    int count = 0;

    /*
     * 扫描整张二值图，遇到未访问过的黑色像素就启动一次 BFS 连通域搜索。
     */
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t idx = at_index(x, y, width);
            if (!s_binary[idx] || s_visited[idx]) continue;

            /*
             * BFS 队列和连通域统计量初始化。
             *
             * min/max 用来得到外接矩形；
             * sum_x/sum_y 用来估算中心点；
             * tl/tr/br/bl 用简单的 x+y、x-y 极值近似四角。
             */
            uint32_t head = 0, tail = 0, area = 0;
            uint32_t min_x = x, max_x = x, min_y = y, max_y = y;
            uint64_t sum_x = 0, sum_y = 0;
            float best_tl = FLT_MAX, best_tr = -FLT_MAX, best_br = -FLT_MAX, best_bl = FLT_MAX;
            at_pt_t tl = {(float)x, (float)y}, tr = {(float)x, (float)y}, br = {(float)x, (float)y}, bl = {(float)x, (float)y};
            s_visited[idx] = 1U;
            s_queue[tail++] = idx;

            /*
             * 8 邻域 BFS，把同一片黑色连通区域全部找出来。
             */
            while (head < tail) {
                uint32_t cur = s_queue[head++];
                uint32_t cx = cur % width;
                uint32_t cy = cur / width;

                area++;
                sum_x += cx;
                sum_y += cy;
                if (cx < min_x) min_x = cx;
                if (cx > max_x) max_x = cx;
                if (cy < min_y) min_y = cy;
                if (cy > max_y) max_y = cy;
                float fsum = (float)cx + (float)cy;
                float fdiff = (float)cx - (float)cy;

                /*
                 * 使用极值估计四角：
                 * - x+y 最小近似左上；
                 * - x-y 最大近似右上；
                 * - x+y 最大近似右下；
                 * - x-y 最小近似左下。
                 */
                if (fsum < best_tl)
                {
                    best_tl = fsum;
                    tl.x = (float)cx;
                    tl.y = (float)cy;
                }
                if (fdiff > best_tr)
                {
                    best_tr = fdiff;
                    tr.x = (float)cx;
                    tr.y = (float)cy;
                }
                if (fsum > best_br)
                {
                    best_br = fsum;
                    br.x = (float)cx;
                    br.y = (float)cy;
                }
                if (fdiff < best_bl)
                {
                    best_bl = fdiff;
                    bl.x = (float)cx;
                    bl.y = (float)cy;
                }

                /*
                 * 扫描当前像素周围 8 邻域。
                 * 边界处 nx0/nx1/ny0/ny1 会自动夹住，避免越界。
                 */
                uint32_t nx0 = (cx > 0U) ? cx - 1U : cx;
                uint32_t nx1 = (cx + 1U < width) ? cx + 1U : cx;
                uint32_t ny0 = (cy > 0U) ? cy - 1U : cy;
                uint32_t ny1 = (cy + 1U < height) ? cy + 1U : cy;
                for (uint32_t ny = ny0; ny <= ny1; ny++) {
                    for (uint32_t nx = nx0; nx <= nx1; nx++) {
                        uint32_t nidx = at_index(nx, ny, width);
                        if (!s_visited[nidx] && s_binary[nidx])
                        {
                            s_visited[nidx] = 1U;
                            s_queue[tail++] = nidx;
                        }
                    }
                }
            }

            /*
             * 连通域搜索完成后先做快速几何过滤。
             */
            uint32_t score = 0;
            if (!at_filter_component(area, min_x, min_y, max_x, max_y, &score)) continue;

            /*
             * 生成候选结构体。
             */
            at_candidate_t cand = {0};
            cand.valid = true;
            cand.area = area;
            cand.min_x = min_x;
            cand.min_y = min_y;
            cand.max_x = max_x;
            cand.max_y = max_y;
            cand.cx = (float)sum_x / (float)area;
            cand.cy = (float)sum_y / (float)area;
            cand.tl = tl;
            cand.tr = tr;
            cand.br = br;
            cand.bl = bl;

            /*
             * 贴边或过大的候选不直接丢弃，而是降低评分。
             * 这样在候选很少时仍然保留一点尝试机会，但优先级更低。
             */
            if (at_candidate_touch_edge(&cand, width, height, AT_EDGE_MARGIN))
            {
                score /= 4U;
            }
            if (at_candidate_too_large(&cand, width, height))
            {
                score /= 4U;
            }
            cand.score = score;
            at_insert_candidate(cands, &count, &cand);
        }
    }
    return count;
}

/* -------------------------------------------------------------------------- */
/* 四边形投影和 bit 解码                                            */
/* -------------------------------------------------------------------------- */

/*
 * 计算两个点之间的平方距离，避免不必要的 sqrt，提高几何判断效率。
 */
static float at_dist2(const at_pt_t *a, const at_pt_t *b)
{
    float dx = a->x - b->x;
    float dy = a->y - b->y;
    return dx * dx + dy * dy;
}
/*
 * 检查候选框是否像一个合理四边形，过滤太小、比例异常或形状不稳定的区域。
 */
static bool at_validate_quad(const at_candidate_t *cand)
{
    float d0 = at_dist2(&cand->tl, &cand->tr);
    float d1 = at_dist2(&cand->tr, &cand->br);
    float d2 = at_dist2(&cand->br, &cand->bl);
    float d3 = at_dist2(&cand->bl, &cand->tl);
    if (d0 < 64.0f || d1 < 64.0f || d2 < 64.0f || d3 < 64.0f) return false;
    if (at_dist2(&cand->tl, &cand->br) < 100.0f) return false;
    if (at_dist2(&cand->tr, &cand->bl) < 100.0f) return false;
    return true;
}
/*
 * 求解从标准 tag 网格坐标到图像四边形坐标的单应矩阵，用于透视校正采样。
 */
static bool at_solve_homography(const at_pt_t quad[4], float H[9])
{
    /*
     * 求从标准 8x8 tag 网格坐标 (u,v) 到图像四边形坐标 (x,y) 的单应矩阵。
     *
     * 四个角点可以提供 8 个方程，正好求解 H 的 8 个自由参数；
     * H[8] 固定为 1。
     */
    float A[8][9];
    const float uv[4][2] = { {0.0f, 0.0f}, {8.0f, 0.0f}, {8.0f, 8.0f}, {0.0f, 8.0f} };

    /*
     * 构造增广矩阵 A。
     * 每个角点贡献两行方程：一行约束 x，一行约束 y。
     */
    for (int i = 0; i < 4; i++) {
        float u = uv[i][0];
        float v = uv[i][1];
        float x = quad[i].x;
        float y = quad[i].y;
        A[2 * i + 0][0] = u;
        A[2 * i + 0][1] = v;
        A[2 * i + 0][2] = 1.0f;
        A[2 * i + 0][3] = 0.0f;
        A[2 * i + 0][4] = 0.0f;
        A[2 * i + 0][5] = 0.0f;
        A[2 * i + 0][6] = -u * x;
        A[2 * i + 0][7] = -v * x;
        A[2 * i + 0][8] = x;
        A[2 * i + 1][0] = 0.0f;
        A[2 * i + 1][1] = 0.0f;
        A[2 * i + 1][2] = 0.0f;
        A[2 * i + 1][3] = u;
        A[2 * i + 1][4] = v;
        A[2 * i + 1][5] = 1.0f;
        A[2 * i + 1][6] = -u * y;
        A[2 * i + 1][7] = -v * y;
        A[2 * i + 1][8] = y;
    }

    /*
     * 高斯-约旦消元。
     * 每一列选择绝对值最大的 pivot，降低数值不稳定风险。
     */
    for (int col = 0; col < 8; col++) {
        int pivot = col;
        float maxv = fabsf(A[col][col]);
        for (int row = col + 1; row < 8; row++) {
            float v = fabsf(A[row][col]);
            if (v > maxv)
            {
                maxv = v;
                pivot = row;
            }
        }
        if (maxv < AT_EPS) return false;

        /*
         * 把 pivot 行换到当前列。
         */
        if (pivot != col)
        {
            for (int k = col; k < 9; k++) {
                float tmp = A[col][k];
                A[col][k] = A[pivot][k];
                A[pivot][k] = tmp;
            }
        }

        /*
         * 当前行归一化，让主元变成 1。
         */
        float div = A[col][col];
        for (int k = col; k < 9; k++) A[col][k] /= div;

        /*
         * 消掉其它行的当前列。
         */
        for (int row = 0; row < 8; row++) {
            if (row == col) continue;
            float factor = A[row][col];
            if (fabsf(factor) < AT_EPS) continue;
            for (int k = col; k < 9; k++) A[row][k] -= factor * A[col][k];
        }
    }

    /*
     * 消元完成后，最后一列就是 H 的前 8 个参数。
     */
    H[0] = A[0][8]; H[1] = A[1][8]; H[2] = A[2][8];
    H[3] = A[3][8]; H[4] = A[4][8]; H[5] = A[5][8];
    H[6] = A[6][8]; H[7] = A[7][8]; H[8] = 1.0f;
    return true;
}
/*
 * 使用单应矩阵把标准网格点投影到原图坐标，找到应该采样的像素位置。
 */
static void at_project(const float H[9], float u, float v, float *x, float *y)
{
    float den = H[6] * u + H[7] * v + H[8];
    if (fabsf(den) < AT_EPS) den = AT_EPS;
    *x = (H[0] * u + H[1] * v + H[2]) / den;
    *y = (H[3] * u + H[4] * v + H[5]) / den;
}
/*
 * 对灰度图做双线性插值采样，比直接取整像素更平滑，减少边界抖动。
 */
static uint8_t at_sample_bilinear(const uint8_t *gray, uint32_t width, uint32_t height, float x, float y)
{
    if (x < 0.0f) x = 0.0f;
    if (y < 0.0f) y = 0.0f;
    if (x > (float)(width - 1U)) x = (float)(width - 1U);
    if (y > (float)(height - 1U)) y = (float)(height - 1U);
    int x0 = (int)floorf(x);
    int y0 = (int)floorf(y);
    int x1 = (x0 + 1 < (int)width) ? x0 + 1 : x0;
    int y1 = (y0 + 1 < (int)height) ? y0 + 1 : y0;
    float ax = x - (float)x0;
    float ay = y - (float)y0;
    float p00 = gray[y0 * (int)width + x0];
    float p10 = gray[y0 * (int)width + x1];
    float p01 = gray[y1 * (int)width + x0];
    float p11 = gray[y1 * (int)width + x1];
    float v0 = p00 + (p10 - p00) * ax;
    float v1 = p01 + (p11 - p01) * ax;
    float v = v0 + (v1 - v0) * ay;
    if (v < 0.0f) v = 0.0f;
    if (v > 255.0f) v = 255.0f;
    return (uint8_t)(v + 0.5f);
}
/*
 * 在某个 tag 单元格中心附近取样，判断该格更偏黑还是偏白。
 */
static uint8_t at_sample_cell(const uint8_t *gray, uint32_t width, uint32_t height, const float H[9], float gx, float gy)
{
    /*
     * 一个格子不只采中心点，而是采中心和上下左右共 5 个点求平均。
     * 这样对轻微模糊、透视误差和像素边界抖动更稳。
     */
    static const float offs[5][2] = { {0.5f, 0.5f}, {0.3f, 0.5f}, {0.7f, 0.5f}, {0.5f, 0.3f}, {0.5f, 0.7f} };
    uint32_t sum = 0;
    for (int i = 0; i < 5; i++) {
        float x, y;
        at_project(H, gx + offs[i][0], gy + offs[i][1], &x, &y);
        sum += at_sample_bilinear(gray, width, height, x, y);
    }
    return (uint8_t)(sum / 5U);
}
/*
 * 把 6x6 数据区顺时针旋转，用于尝试 tag 的不同朝向。
 */
static void at_rotate_bits_cw(const uint8_t src[AT_DATA_GRID][AT_DATA_GRID], uint8_t dst[AT_DATA_GRID][AT_DATA_GRID])
{
    for (uint32_t r = 0; r < AT_DATA_GRID; r++) {
        for (uint32_t c = 0; c < AT_DATA_GRID; c++) {
            dst[r][c] = src[AT_DATA_GRID - 1U - c][r];
        }
    }
}
/*
 * 按旋转/翻转方式变换 6x6 bit 矩阵，用于尝试不同方向或镜像情况下的编码匹配。
 */
static void at_transform_bits(const uint8_t src[AT_DATA_GRID][AT_DATA_GRID],
                              at_bits_transform_t tf,
                              uint8_t dst[AT_DATA_GRID][AT_DATA_GRID])
{
    /*
     * 对 6x6 数据区做不同几何变换。
     *
     * 这些变换用于提高对安装方向、镜像、采样顺序差异的容错能力。
     * 最终会选择汉明距离最小的那一种。
     */
    for (uint32_t r = 0; r < AT_DATA_GRID; r++) {
        for (uint32_t c = 0; c < AT_DATA_GRID; c++) {
            switch (tf) {
            case AT_BITS_IDENTITY:
                dst[r][c] = src[r][c];
                break;
            case AT_BITS_MIRROR_X:
                dst[r][c] = src[r][AT_DATA_GRID - 1U - c];
                break;
            case AT_BITS_MIRROR_Y:
                dst[r][c] = src[AT_DATA_GRID - 1U - r][c];
                break;
            case AT_BITS_TRANSPOSE:
                dst[r][c] = src[c][r];
                break;
            case AT_BITS_TRANSPOSE_MIRROR_X:
                dst[r][c] = src[c][AT_DATA_GRID - 1U - r];
                break;
            case AT_BITS_TRANSPOSE_MIRROR_Y:
                dst[r][c] = src[AT_DATA_GRID - 1U - c][r];
                break;
            default:
                dst[r][c] = src[r][c];
                break;
            }
        }
    }
}
/*
 * 按 Tag36h11 码族约定顺序把 6x6 数据位打包成 64 位编码。
 */
static uint64_t at_build_code_family(const uint8_t bits[AT_DATA_GRID][AT_DATA_GRID], bool lsb_first)
{
    uint64_t code = 0;
    if (!lsb_first)
    {
        for (int i = 0; i < 36; i++) {
            uint8_t x = s_tag36h11_bit_x[i] - 1U;
            uint8_t y = s_tag36h11_bit_y[i] - 1U;
            code = (code << 1) | (bits[y][x] ? 1ULL : 0ULL);
        }
        return code;
    }
    for (int i = 0; i < 36; i++) {
        uint8_t x = s_tag36h11_bit_x[i] - 1U;
        uint8_t y = s_tag36h11_bit_y[i] - 1U;
        if (bits[y][x])
        {
            code |= (1ULL << i);
        }
    }
    return code;
}
/*
 * 按行优先方式把 6x6 bit 打包成编码，用于兼容不同采样/码表组织方式。
 */
static uint64_t at_build_code_row_major(const uint8_t bits[AT_DATA_GRID][AT_DATA_GRID], bool lsb_first)
{
    uint64_t code = 0;
    uint32_t idx = 0;
    for (uint32_t r = 0; r < AT_DATA_GRID; r++) {
        for (uint32_t c = 0; c < AT_DATA_GRID; c++, idx++) {
            if (!lsb_first)
            {
                code = (code << 1) | (bits[r][c] ? 1ULL : 0ULL);
            }
            else if (bits[r][c])
            {
                code |= (1ULL << idx);
            }
        }
    }
    return code;
}
/*
 * 按列优先方式把 6x6 bit 打包成编码，用于辅助排查或兼容不同 bit 顺序。
 */
static uint64_t at_build_code_col_major(const uint8_t bits[AT_DATA_GRID][AT_DATA_GRID], bool lsb_first)
{
    uint64_t code = 0;
    uint32_t idx = 0;
    for (uint32_t c = 0; c < AT_DATA_GRID; c++) {
        for (uint32_t r = 0; r < AT_DATA_GRID; r++, idx++) {
            if (!lsb_first)
            {
                code = (code << 1) | (bits[r][c] ? 1ULL : 0ULL);
            }
            else if (bits[r][c])
            {
                code |= (1ULL << idx);
            }
        }
    }
    return code;
}
/*
 * 根据指定打包模式生成候选编码，然后与合法 Tag36h11 码表匹配。
 */
static uint64_t at_build_code_variant(const uint8_t bits[AT_DATA_GRID][AT_DATA_GRID], at_pack_mode_t mode)
{
    switch (mode) {
    case AT_PACK_FAMILY_MSB:
        return at_build_code_family(bits, false);
    case AT_PACK_FAMILY_LSB:
        return at_build_code_family(bits, true);
    case AT_PACK_ROW_MSB:
        return at_build_code_row_major(bits, false);
    case AT_PACK_ROW_LSB:
        return at_build_code_row_major(bits, true);
    case AT_PACK_COL_MSB:
        return at_build_code_col_major(bits, false);
    case AT_PACK_COL_LSB:
        return at_build_code_col_major(bits, true);
    default:
        return 0ULL;
    }
}
/*
 * 遍历 Tag36h11 码表，找出与当前编码汉明距离最小的 tag ID。
 */
static void at_match_code(uint64_t code, uint16_t *best_id, uint32_t *best_h)
{
    *best_h = UINT32_MAX;
    *best_id = 0;
    for (uint16_t id = 0; id < 587U; id++) {
        uint32_t h = at_hamming64(code, s_tag36h11_codes[id]);
        if (h < *best_h)
        {
            *best_h = h;
            *best_id = id;
            if (h == 0U) break;
        }
    }
}
/*
 * 对单个候选四边形完成透视采样、边框校验、bit 解码和码表匹配。
 */
static bool at_decode_with_quad(const uint8_t *gray,
                                uint32_t width,
                                uint32_t height,
                                const at_candidate_t *cand,
                                app_apriltag_result_t *out)
{
    /*
     * 候选四角按标准顺序：左上、右上、右下、左下。
     */
    at_pt_t quad[4] = { cand->tl, cand->tr, cand->br, cand->bl };
    float H[9];

    /*
     * 先做四边形基本几何校验，再求透视单应矩阵。
     */
    if (!at_validate_quad(cand))
    {
        return false;
    }
    if (!at_solve_homography(quad, H))
    {
        return false;
    }

    /*
     * 对标准 8x8 tag 网格逐格采样。
     *
     * 外圈是 AprilTag 的黑色边框；
     * 内部 6x6 是 Tag36h11 的数据位。
     */
    uint8_t cells[AT_GRID_SIZE][AT_GRID_SIZE];
    uint32_t border_sum = 0;
    uint8_t inner_vals[AT_DATA_GRID * AT_DATA_GRID];
    uint32_t inner_idx = 0;
    for (uint32_t gy = 0; gy < AT_GRID_SIZE; gy++) {
        for (uint32_t gx = 0; gx < AT_GRID_SIZE; gx++) {
            uint8_t v = at_sample_cell(gray, width, height, H, (float)gx, (float)gy);
            cells[gy][gx] = v;
            if (gx == 0U || gy == 0U || gx == (AT_GRID_SIZE - 1U) || gy == (AT_GRID_SIZE - 1U))
            {
                border_sum += v;
            }
            else
            {
                inner_vals[inner_idx++] = v;
            }
        }
    }

    /*
     * 对内部 6x6 灰度值排序。
     * 后面用最暗 12 个和最亮 12 个估计黑/白均值，
     * 比直接用全局 Otsu 阈值更适合单个候选区域。
     */
    for (uint32_t i = 0; i < inner_idx; i++) {
        for (uint32_t j = i + 1; j < inner_idx; j++) {
            if (inner_vals[j] < inner_vals[i])
            {
                uint8_t t = inner_vals[i]; inner_vals[i] = inner_vals[j]; inner_vals[j] = t;
            }
        }
    }

    /*
     * 估计黑白阈值。
     *
     * border_sum 来自外圈黑框，dark_mean/white_mean 来自内部数据区。
     * 如果图像对比度太低，就放弃这个候选。
     */
    uint32_t black_mean = border_sum / AT_RING_CELLS;
    uint32_t bright_sum = 0;
    uint32_t dark_sum = 0;
    for (uint32_t i = 0; i < 12; i++) dark_sum += inner_vals[i];
    for (uint32_t i = inner_idx - 12; i < inner_idx; i++) bright_sum += inner_vals[i];
    uint32_t dark_mean = dark_sum / 12U;
    uint32_t white_mean = bright_sum / 12U;
    if (black_mean > dark_mean) black_mean = dark_mean;
    if (white_mean <= black_mean + 18U)
    {
        return false;
    }

    uint8_t threshold = (uint8_t)((black_mean + white_mean) / 2U);

    /*
     * 校验外圈黑框。
     * Tag36h11 外圈应大部分为黑色；如果黑色比例太低，说明候选不像 tag。
     */
    uint32_t border_dark = 0;
    for (uint32_t i = 0; i < AT_GRID_SIZE; i++) {
        border_dark += (cells[0][i] < threshold);
        border_dark += (cells[AT_GRID_SIZE - 1U][i] < threshold);
        if (i > 0U && i < (AT_GRID_SIZE - 1U))
        {
            border_dark += (cells[i][0] < threshold);
            border_dark += (cells[i][AT_GRID_SIZE - 1U] < threshold);
        }
    }
    uint8_t border_pct = (uint8_t)((border_dark * 100U) / AT_RING_CELLS);
    if (border_pct < 85U)
    {
        return false;
    }

    /*
     * 把内部 6x6 灰度采样转成 0/1 bit。
     */
    uint8_t bits0[AT_DATA_GRID][AT_DATA_GRID] = {0};
    for (uint32_t gy = 0; gy < AT_DATA_GRID; gy++) {
        for (uint32_t gx = 0; gx < AT_DATA_GRID; gx++) {
            bits0[gy][gx] = (cells[gy + 1U][gx + 1U] > threshold) ? 1U : 0U;
        }
    }

    /*
     * 尝试 4 个旋转角、多个镜像/转置变换、多个 bit 打包顺序。
     *
     * 这样做比只认一种方向更鲁棒，尤其适合摄像头安装方向、画面翻转还在调试阶段的工程。
     */
    uint32_t best_hamming = 64U;
    uint16_t best_id = 0;
    uint8_t best_rot = 0;
    uint8_t bits_rot[AT_DATA_GRID][AT_DATA_GRID];
    uint8_t bits_tmp[AT_DATA_GRID][AT_DATA_GRID];
    uint8_t bits_tf[AT_DATA_GRID][AT_DATA_GRID];
    memcpy(bits_rot, bits0, sizeof(bits_rot));
    for (uint8_t rot = 0; rot < 4U; rot++) {
        for (int tf = 0; tf < (int)AT_BITS_TRANSFORM_COUNT; tf++) {
            at_transform_bits(bits_rot, (at_bits_transform_t)tf, bits_tf);
            for (int pack = 0; pack < (int)AT_PACK_MODE_COUNT; pack++) {
                uint64_t code = at_build_code_variant(bits_tf, (at_pack_mode_t)pack);
                uint16_t local_id = 0;
                uint32_t local_h = UINT32_MAX;
                at_match_code(code, &local_id, &local_h);

                /*
                 * 记录所有尝试中的全局最佳匹配。
                 */
                if (local_h < best_hamming)
                {
                    best_hamming = local_h;
                    best_id = local_id;
                    best_rot = rot;
                    if (best_hamming == 0U)
                    {
                        break;
                    }
                }
            }
            if (best_hamming == 0U)
            {
                break;
            }
        }
        if (best_hamming == 0U) break;

        /*
         * 当前旋转角没有完美匹配，继续尝试顺时针旋转 90 度。
         */
        at_rotate_bits_cw(bits_rot, bits_tmp);
        memcpy(bits_rot, bits_tmp, sizeof(bits_rot));
    }
    if (best_hamming > AT_MAX_HAMMING)
    {
        return false;
    }

    /*
     * 匹配成功，把候选几何信息和解码信息写入输出结果。
     */
    memset(out, 0, sizeof(*out));
    out->valid = true;
    out->id = best_id;
    out->hamming = (uint8_t)best_hamming;
    out->rotation = best_rot;
    out->threshold = threshold;
    out->border_dark_pct = border_pct;
    out->center_x = (int32_t)lroundf(cand->cx);
    out->center_y = (int32_t)lroundf(cand->cy);
    out->area = (int32_t)cand->area;
    out->bbox_x = (int32_t)cand->min_x;
    out->bbox_y = (int32_t)cand->min_y;
    out->bbox_w = (int32_t)(cand->max_x - cand->min_x + 1U);
    out->bbox_h = (int32_t)(cand->max_y - cand->min_y + 1U);
    out->corner_tl_x = (int32_t)lroundf(cand->tl.x);
    out->corner_tl_y = (int32_t)lroundf(cand->tl.y);
    out->corner_tr_x = (int32_t)lroundf(cand->tr.x);
    out->corner_tr_y = (int32_t)lroundf(cand->tr.y);
    out->corner_br_x = (int32_t)lroundf(cand->br.x);
    out->corner_br_y = (int32_t)lroundf(cand->br.y);
    out->corner_bl_x = (int32_t)lroundf(cand->bl.x);
    out->corner_bl_y = (int32_t)lroundf(cand->bl.y);

    /*
     * 计算四条边的平均像素长度，用于后续粗略距离估算。
     */
    const float edge_top = sqrtf(at_dist2(&cand->tl, &cand->tr));
    const float edge_right = sqrtf(at_dist2(&cand->tr, &cand->br));
    const float edge_bottom = sqrtf(at_dist2(&cand->br, &cand->bl));
    const float edge_left = sqrtf(at_dist2(&cand->bl, &cand->tl));
    out->edge_px_avg = (edge_top + edge_right + edge_bottom + edge_left) * 0.25f;

    /*
     * 顶边角度用于 UI/调试观察 tag 是否倾斜。
     */
    out->top_edge_angle_deg = atan2f(cand->tr.y - cand->tl.y,
                                     cand->tr.x - cand->tl.x) * (180.0f / (float)M_PI);
    return true;
}

/* -------------------------------------------------------------------------- */
/* 公开接口                                                                  */
/* -------------------------------------------------------------------------- */

/*
 * 初始化 AprilTag 模块所需的工作缓冲区，例如二值图、访问标记和 BFS 队列。
 */
esp_err_t app_apriltag_init(void)
{
    if (s_inited) return ESP_OK;
    s_binary = (uint8_t *)heap_caps_malloc(AT_MAX_PIXELS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_visited = (uint8_t *)heap_caps_malloc(AT_MAX_PIXELS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_queue = (uint32_t *)heap_caps_malloc(sizeof(uint32_t) * AT_MAX_PIXELS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_binary == NULL || s_visited == NULL || s_queue == NULL)
    {

        if (s_binary) heap_caps_free(s_binary);

        if (s_visited) heap_caps_free(s_visited);

        if (s_queue) heap_caps_free(s_queue);
        s_binary = NULL;
        s_visited = NULL;
        s_queue = NULL;

        return ESP_ERR_NO_MEM;
    }
    s_inited = true;
    ESP_LOGI(TAG, "local perspective tag36h11 detector ready");
    return ESP_OK;
}
/*
 * 对一帧灰度图执行完整 Tag36h11 检测，输出 ID、中心点、边框、角点、汉明距离等结果。
 */
bool app_apriltag_detect_tag36h11(const uint8_t *gray,
                                  uint32_t width,
                                  uint32_t height,
                                  app_apriltag_result_t *out)
{
    /*
     * 必须先调用 app_apriltag_init() 分配工作缓冲。
     */
    if (!s_inited) return false;

    if (gray == NULL || out == NULL || width == 0U || height == 0U) return false;

    /*
     * 限制输入尺寸，确保 s_binary/s_visited/s_queue 缓冲不会越界。
     */
    if (width > AT_MAX_WIDTH || height > AT_MAX_HEIGHT || (width * height) > AT_MAX_PIXELS) return false;

    memset(out, 0, sizeof(*out));

    /*
     * 第一步：根据当前帧光照自动计算阈值，并二值化。
     */
    uint8_t threshold = at_otsu_threshold(gray, width, height);
    at_threshold_binary(gray, width, height, threshold);

    /*
     * 第二步：在二值图中收集可能的 tag 候选区域。
     */
    at_candidate_t cands[AT_MAX_CANDIDATES] = {0};
    int cand_count = at_collect_candidates(width, height, cands);
    if (cand_count <= 0)
    {
        return false;
    }

    /*
     * 第三步：逐个候选做透视采样和码表匹配，保留最佳结果。
     */
    bool found = false;
    app_apriltag_result_t best = {0};
    for (int i = 0; i < cand_count; i++) {
        /*
         * 贴边或过大的候选在最终解码前再过滤一次。
         */
        if (at_candidate_touch_edge(&cands[i], width, height, AT_EDGE_MARGIN))
        {
            continue;
        }
        if (at_candidate_too_large(&cands[i], width, height))
        {
            continue;
        }
        app_apriltag_result_t cur;
        bool ok = at_decode_with_quad(gray, width, height, &cands[i], &cur);
        if (!ok) continue;

        /*
         * 多个候选都能解码时，优先选择：
         * 1. 汉明距离更小；
         * 2. 黑边比例更高；
         * 3. 区域面积更大。
         */
        if (!found || cur.hamming < best.hamming ||
            (cur.hamming == best.hamming && cur.border_dark_pct > best.border_dark_pct) ||
            (cur.hamming == best.hamming && cur.border_dark_pct == best.border_dark_pct && cur.area > best.area))
        {
            best = cur;
            found = true;
        }
    }
    if (!found)
    {
        return false;
    }

    /*
     * 如果候选解码没有写入 threshold，就回填整帧 Otsu 阈值，方便调试显示。
     */
    if (best.threshold == 0U) best.threshold = threshold;
    *out = best;
    return true;
}
