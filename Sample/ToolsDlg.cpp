#include "stdafx.h"
#include "Sample.h"
#include "ToolsDlg.h"
#include "PresetDlg.h"
#include "SessionDlg.h"
#include "LogDlg.h"
#include "DisplayConfigDlg.h"
#include "DialogUserPreset.h"
#include "LogMgr.h"
#include <algorithm>
#include "ConfigMgr.h"
#include "UserPresets.h"
#include "aes.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// 格式化码率，返回易读性的码率字符串
static void inline FriendlyBitrate(CString& fmt, int bitrate) {
    if (bitrate > 1000 * 1000) {
        fmt.Format(_T("%.2f mbps"), bitrate / 1000000.0f);
    } else if (bitrate > 1000) {
        fmt.Format(_T("%d kbps"), bitrate / 1000);
    } else  {
        fmt.Format(_T("%d bps"), bitrate);
    }
}

// 视频信息排序比较函数对象
struct VideoInfoPred {
    int operator()(const lc_video_info_t& i1, const lc_video_info_t& i2) {
        return !less_than(i1, i2);
    }
    int less_than(const lc_video_info_t& i1, const lc_video_info_t& i2) {
        if (i1.width_in_pixel < i2.width_in_pixel) {
            return 1;
        } else if (i1.width_in_pixel > i2.width_in_pixel) {
            return 0;
        }

        if (i1.height_in_pixel < i2.height_in_pixel) {
            return 1;
        } else if (i1.height_in_pixel > i2.height_in_pixel) {
            return 0;
        }

        if (i1.avg_time_per_frame_in_100ns > i2.avg_time_per_frame_in_100ns) {
            return 1;
        } else if (i1.avg_time_per_frame_in_100ns < i2.avg_time_per_frame_in_100ns) {
            return 0;
        }

        if (i1.pixel_depth_in_bit < i2.pixel_depth_in_bit) {
            return 1;
        } else if (i1.pixel_depth_in_bit > i2.pixel_depth_in_bit) {
            return 0;
        }

        if (i1.interlaced > i1.interlaced) {
            return 1;
        } else if (i1.interlaced < i2.interlaced) {
            return 0;
        }

        if (i1.format_fourcc < i2.format_fourcc) {
            return 1;
        }

        return 0;
    }
};

CString video_info_string(const lc_video_info_t& info) {
    USES_CONVERSION;
    CString strFmt;
    TCHAR fourcc[5] = { 0 };

    if (info.format_fourcc <= BI_BITFIELDS) {
        _tcsncpy_s(fourcc, _T("RGB"), -1);
    } else {
        _tcsncpy_s(fourcc, A2T((const char*)(&info.format_fourcc)), 4);
    }

    strFmt.Format(_T("%dx%d %.2ffps %s %dbit %s"),
                  info.width_in_pixel, info.height_in_pixel,
                  info.avg_time_per_frame_in_100ns ?
                  10000000.0f / info.avg_time_per_frame_in_100ns :
                  0.0f,
                  fourcc, info.pixel_depth_in_bit,
                  info.interlaced ? _T("i") : _T("p"));

    return strFmt;
}

// 设置日志回调函数，将日志转入日志管理类
void LC_API lc_log_callback(LC_LOGLEVEL level, const char* msg) {
    USES_CONVERSION;
    CLogMgr::Instance().AppendLog(level, A2T(msg));
}

int IsDisplay(LPCTSTR name, int& dispId) {
    if (_tcsnicmp(name, _T("display"), 7) == 0) {
        dispId = _ttoi(name + 7);
        return TRUE;
    } else {
        return FALSE;
    }
}


#define WM_USER_STATUS_CHANGED (WM_APP +1)

CToolsDlg::CToolsDlg(CWnd* pParent /*=NULL*/)
    : CDialog(CToolsDlg::IDD, pParent)
    , m_handle(0)
    , m_presets(0)
    , m_pLogDlg(NULL)
    , m_pPresetDlg(NULL)
    , m_pSessionDlg(NULL)
    , m_pNotificationDlg(NULL)
    , m_pOptionDlg(NULL)
    , m_uTimer(0)
    , m_bAuth(FALSE) {
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
    memset(&m_arDisplayInfo[0], 0, sizeof(m_arDisplayInfo));
}

CToolsDlg::~CToolsDlg() {
    if (m_handle) {
        lc_close(m_handle);
        m_handle = 0;
    }

    if (m_presets) {
        lc_list_free(m_presets);
        m_presets = 0;
    }
}

void CToolsDlg::DoDataExchange(CDataExchange* pDX) {
    CDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_BUTTON_AUTO_PRESET, m_BtnDetectPreset);
    DDX_Control(pDX, IDC_BUTTON_SELECT_PATH, m_BtnSelectPath);
    DDX_Control(pDX, IDC_BUTTON_CLOSE, m_BtnClose);
    DDX_Control(pDX, IDC_BUTTON_STOP, m_BtnStop);
    DDX_Control(pDX, IDC_BUTTON_START, m_BtnStart);
    DDX_Control(pDX, IDC_COMBO_VIDEO, m_CmbVideoDevices);
    DDX_CBString(pDX, IDC_COMBO_VIDEO, m_Model.m_VideoDevice);
    DDX_CBString(pDX, IDC_COMBO_VIDEO2, m_Model.m_VideoDevice2);
    DDX_Control(pDX, IDC_COMBO_AUDIO, m_CmbAudioDevices);
    DDX_CBString(pDX, IDC_COMBO_AUDIO, m_Model.m_AudioDevice);
    DDX_Control(pDX, IDC_COMBO_PRESET, m_CmbPresets);
    DDX_CBString(pDX, IDC_COMBO_PRESET, m_Model.m_Preset);
    DDX_Control(pDX, IDC_CHECK_SAVE_LOCAL, m_ChkSaveLocal);
    DDX_Check(pDX, IDC_CHECK_SAVE_LOCAL, m_Model.m_SaveLocal);
    DDX_Control(pDX, IDC_EDIT_LOCAL_PATH, m_EdtLocalPath);
    DDX_Text(pDX, IDC_EDIT_LOCAL_PATH, m_Model.m_LocalSavePath);
    DDX_Control(pDX, IDC_EDIT_RTMP, m_EdtUserRtmp);
    DDX_Radio(pDX, IDC_RADIO_EXISTING_SESSION, m_Model.m_RtmpOption);
    DDX_Text(pDX, IDC_EDIT_RTMP, m_Model.m_UserRTMPUrl);
    DDX_Control(pDX, IDC_EDIT_HLS_URL, m_EdtHLSPlayUrl);
    DDX_Text(pDX, IDC_EDIT_HLS_URL, m_Model.m_HLSPlayUrl);
    DDX_Control(pDX, IDC_EDIT_RTMP_URL, m_EdtRtmpPlayUrl);
    DDX_Text(pDX, IDC_EDIT_RTMP_URL, m_Model.m_RTMPPlayUrl);
    DDX_Control(pDX, IDC_STATIC_STATUS, m_staStatus);
    DDX_Text(pDX, IDC_STATIC_STATUS, m_Model.m_Status);
    DDX_Control(pDX, IDC_EDIT_ERROR_DETAIL, m_edtErrorDetail);
    DDX_Control(pDX, IDC_RADIO_EXISTING_SESSION, m_rdoExistRtmp);
    DDX_Control(pDX, IDC_RADIO_RTMP, m_rdoUserRtmp);
    DDX_Control(pDX, IDC_BUTTON_REFRESH_DATA, m_btnRefresh);
    DDX_Control(pDX, IDC_EDIT_SESSION, m_edtExistingSession);
    DDX_Control(pDX, IDC_BUTTON_SELECT_SESSION, m_btnSelectSession);
    DDX_Text(pDX, IDC_EDIT_SESSION, m_Model.m_BCESessionId);
    DDX_Control(pDX, IDC_COMBO_VIDEO_INFO, m_cmbVideoInfos);
    DDX_Control(pDX, IDC_EDIT_STATIS, m_edtStatis);
    DDX_Control(pDX, IDC_CHECK_VIDEO2, m_chkVideo2);
    DDX_Control(pDX, IDC_COMBO_VIDEO2, m_cmbVideo2);
    DDX_Control(pDX, IDC_COMBO_VIDEO_INFO2, m_cmbVideoInfo2);
    DDX_Control(pDX, IDC_EDIT_VIDEO_X, m_edtVideoX);
    DDX_Control(pDX, IDC_EDIT_VIDEO_Y, m_edtVideoY);
    DDX_Control(pDX, IDC_EDIT_VIDEO2_X, m_edtVideo2X);
    DDX_Control(pDX, IDC_EDIT_VIDEO2_Y, m_edtVideo2Y);
    DDX_Control(pDX, IDC_EDIT_VIDEO_WIDTH, m_edtVideoWidth);
    DDX_Control(pDX, IDC_EDIT_VIDEO_HEIGHT, m_edtVideoHeight);
    DDX_Control(pDX, IDC_EDIT_VIDEO2_WIDTH, m_edtVideo2Width);
    DDX_Control(pDX, IDC_EDIT_VIDEO2_HEIGHT, m_edtVideo2Height);

    DDX_Check(pDX, IDC_CHECK_VIDEO2, m_Model.m_bTwoVideoSrc);

    DDX_Text(pDX, IDC_EDIT_VIDEO_X, m_Model.m_ptVideo.x);
    DDX_Text(pDX, IDC_EDIT_VIDEO_Y, m_Model.m_ptVideo.y);
    DDX_Text(pDX, IDC_EDIT_VIDEO2_X, m_Model.m_ptVideo2.x);
    DDX_Text(pDX, IDC_EDIT_VIDEO2_Y, m_Model.m_ptVideo2.y);

    DDX_Text(pDX, IDC_EDIT_VIDEO_WIDTH, m_Model.m_szVideo.cx);
    DDX_Text(pDX, IDC_EDIT_VIDEO_HEIGHT, m_Model.m_szVideo.cy);
    DDX_Text(pDX, IDC_EDIT_VIDEO2_WIDTH, m_Model.m_szVideo2.cx);
    DDX_Text(pDX, IDC_EDIT_VIDEO2_HEIGHT, m_Model.m_szVideo2.cy);
    DDX_Control(pDX, IDC_BTN_VIDEO_CONFIG, m_btnVideoConfig);
    DDX_Control(pDX, IDC_BTN_VIDEO_CONFIG2, m_btnVideoConfig2);
    DDX_Control(pDX, IDC_BUTTON_EDIT_PRESET, m_BtnUserPreset);
}

BEGIN_MESSAGE_MAP(CToolsDlg, CDialog)
    ON_WM_PAINT()
    ON_WM_QUERYDRAGICON()
    ON_BN_CLICKED(IDC_BUTTON_AUTO_PRESET, &CToolsDlg::OnBnClickedButtonAutoPreset)
    ON_BN_CLICKED(IDC_BUTTON_SELECT_PATH, &CToolsDlg::OnBnClickedButtonSelectPath)
    ON_BN_CLICKED(IDC_BUTTON_CLOSE, &CToolsDlg::OnBnClickedButtonClose)
    ON_BN_CLICKED(IDC_BUTTON_STOP, &CToolsDlg::OnBnClickedButtonStop)
    ON_BN_CLICKED(IDC_BUTTON_START, &CToolsDlg::OnBnClickedButtonStart)
    ON_BN_CLICKED(IDC_RADIO_RTMP, &CToolsDlg::OnBnClickedRadioRtmp)
    ON_BN_CLICKED(IDC_CHECK_SAVE_LOCAL, &CToolsDlg::OnBnClickedCheckSaveLocal)
    ON_BN_CLICKED(IDC_BUTTON_REFRESH_DATA, &CToolsDlg::OnBnClickedButtonRefreshData)
    ON_BN_CLICKED(IDC_BUTTON_EDIT_PRESET, &CToolsDlg::OnBnClickedUserPreset)
    ON_BN_CLICKED(IDC_BTN_VIDEO_CONFIG, &CToolsDlg::OnBnClickedButtonVideoConfig)
    ON_BN_CLICKED(IDC_BTN_VIDEO_CONFIG2, &CToolsDlg::OnBnClickedButtonVideoConfig2)
    ON_MESSAGE(WM_USER_STATUS_CHANGED, OnStatusChanged_WM)
    ON_WM_TIMER()
    ON_MESSAGE(WM_IDLEUPDATECMDUI, &CToolsDlg::OnIdleUpdateCmdUI)
    ON_COMMAND(IDM_PRESET, &CToolsDlg::OnMenuPreset)
    ON_WM_SYSCOMMAND()
    ON_UPDATE_COMMAND_UI(IDM_PRESET, &CToolsDlg::OnUpdateMenuPreset)
    ON_COMMAND(IDM_SESSION, &CToolsDlg::OnMenuSession)
    ON_UPDATE_COMMAND_UI(IDM_SESSION, &CToolsDlg::OnUpdateMenuSession)
    ON_COMMAND(IDM_LOG, &CToolsDlg::OnMenuLog)
    ON_UPDATE_COMMAND_UI(IDM_LOG, &CToolsDlg::OnUpdateMenuLog)
    ON_WM_DESTROY()
    ON_BN_CLICKED(IDC_BUTTON_SELECT_SESSION, &CToolsDlg::OnBnClickedButtonSelectSession)
    ON_BN_CLICKED(IDC_RADIO_EXISTING_SESSION, &CToolsDlg::OnBnClickedRadioExistingSession)
    ON_CBN_SELCHANGE(IDC_COMBO_VIDEO_INFO, &CToolsDlg::OnCbnSelchangeComboVideoInfo)
    ON_CBN_SELCHANGE(IDC_COMBO_PRESET, &CToolsDlg::OnCbnSelchangeComboPreset)
    ON_CBN_SELCHANGE(IDC_COMBO_VIDEO, &CToolsDlg::OnCbnSelchangeComboVideo)
    ON_CBN_SELCHANGE(IDC_COMBO_VIDEO2, &CToolsDlg::OnCbnSelchangeComboVideo2)
    ON_WM_CREATE()
    ON_COMMAND(IDM_OPTION, &CToolsDlg::OnOption)
    ON_COMMAND(IDM_NOTIFICATION, &CToolsDlg::OnNotification)
    ON_UPDATE_COMMAND_UI(IDM_NOTIFICATION, &CToolsDlg::OnUpdateNotification)
    ON_UPDATE_COMMAND_UI(IDM_OPTION, &CToolsDlg::OnUpdateOption)
    ON_BN_CLICKED(IDC_CHECK_VIDEO2, &CToolsDlg::OnBnClickedCheckVideo2)
END_MESSAGE_MAP()


BOOL CToolsDlg::DoAuth(lc_bce_access_key_t* key, CString& host, BOOL enableConfig,
                       BOOL enablePrompt /* = TRUE*/) {
    USES_CONVERSION;
    CString strmsg;
    BOOL provided = key->access_key_id[0] && key->secret_access_key[0];

    // 初始化采集SDK
    if (provided && LC_OK != lc_init(key, T2A(host))) {
        m_bAuth = FALSE;
        CLogMgr::Instance().AppendLog(LC_LOG_ERROR, _T("lc_init failed."));

        if (enableConfig) {
            if (IDOK == AfxMessageBox(_T("AK/SK验证失败\r\n")
                                      _T("点击\"确定\"重新配置\r\n")
                                      _T("点击\"取消\"使用指定RTMP模式"), MB_OK)) {
                OnOption();
            } else {
                if (enablePrompt) {
                    AfxMessageBox(_T("AK/SK验证失败\r\n"));
                }
            }
        } else {
            CLogMgr::Instance().AppendLog(LC_LOG_ERROR, A2T(lc_get_last_error()));

            if (enablePrompt) {
                AfxMessageBox(_T("AK/SK不匹配,认证失败."));
            }
        }
    } else {
        lc_session_t session = {0};

        if (provided && LC_OK == lc_session_query("auth-test", &session)) {
            m_bAuth = TRUE;
        } else {
            CString code = _T("LiveExceptions.NoSuchSession");

            if (provided && _strnicmp(lc_get_last_error(), T2A(code), code.GetLength()) == 0) {
                m_bAuth = TRUE;
            } else if (!provided || _strnicmp(lc_get_last_error(), "autherror", 9) == 0) {
                m_bAuth = FALSE;

                if (enableConfig) {
                    strmsg.Format(_T("%s\r\n%s"), provided ? _T("鉴权失败") : _T("AK/SK未正确配置."),
                                  _T("点击\"确定\"打开配置界面\r\n")
                                  _T("点击\"取消\"使用指定RTMP模式"));

                    if (IDOK == AfxMessageBox(strmsg, MB_OKCANCEL)) {
                        OnOption();
                    }
                } else {
                    CLogMgr::Instance().AppendLog(LC_LOG_ERROR, A2T(lc_get_last_error()));

                    if (enablePrompt) {
                        strmsg.Format(_T("%s"), provided ? _T("AK/SK不匹配,认证失败.") :
                                      _T("AK/SK未正确配置."));
                        AfxMessageBox(strmsg);
                    }
                }
            } else {
                m_bAuth = FALSE;
                CLogMgr::Instance().AppendLog(LC_LOG_ERROR, A2T(lc_get_last_error()));

                if (enablePrompt) {
                    strmsg.Format(_T("认证失败\r\n%s"), A2T(lc_get_last_error()));
                    AfxMessageBox(strmsg);
                }
            }
        }
    }

    OnIdleUpdateCmdUI(0, 0);
    DrawMenuBar();
    UpdateAuthMode();

    return m_bAuth;
}

BOOL CToolsDlg::OnInitDialog() {
    USES_CONVERSION;

    m_Model.Deserailize();

    CDialog::OnInitDialog();

    SetIcon(m_hIcon, TRUE);
    SetIcon(m_hIcon, FALSE);

    CString ak;
    CString sk;
    CString host;

    ConfigMgr::Instance().GetAk(ak);
    ConfigMgr::Instance().GetSk(sk);
    ConfigMgr::Instance().GetHost(host);

    AES aes;

    aes.Decrypt(ak, ak);
    aes.Decrypt(sk, sk);

    lc_bce_access_key_t key = { 0 };
    strncpy_s(key.access_key_id, T2A(ak), -1);
    strncpy_s(key.secret_access_key, T2A(sk), -1);

    DoAuth(&key, host, TRUE);

    // 设置日志回调
    lc_log_set_callback(lc_log_callback);

    //更新视频、音频设备，更新录制模板列表
    UpdateLiveCaptureData();

    // 更新界面状态
    EnableUI(TRUE);
    UpdateStatus();

    EnableToolTips();

    return TRUE;
}

// 更新视频设备，音频设备，和录制模板列表
void CToolsDlg::UpdateLiveCaptureData() {
    USES_CONVERSION;

    CString oldAudioName;
    int oldAudioIndex = m_CmbAudioDevices.GetCurSel();

    // 保存旧的音频设备名称
    if (oldAudioIndex != CB_ERR) {
        m_CmbAudioDevices.GetLBText(oldAudioIndex, oldAudioName);
    }

    // 刷新音频设备ComboBox
    m_CmbAudioDevices.ResetContent();
    lc_audio_device_t audioDevice = { 0 };
    // 调用API, 得到音频设备数量
    int audios = lc_audio_device_get_count();

    for (int i = 0; i < audios; i ++) {
        // 调用API, 得到第i个音频设备信息
        if (LC_OK == lc_audio_device_get_device(i, &audioDevice)) {
            m_CmbAudioDevices.AddString(A2T(audioDevice.device_name));
        }
    }

    // 根据旧的音频设备，选择新的音频设备
    if (oldAudioName.IsEmpty() ||
            m_CmbAudioDevices.SelectString(-1, oldAudioName) < 0) {
        m_CmbAudioDevices.SetCurSel(0);
    }


    // 保存旧的视频设备名称
    CString oldVideoName;
    int oldVideoIndex = m_CmbVideoDevices.GetCurSel();

    if (oldVideoIndex != CB_ERR) {
        m_CmbVideoDevices.GetLBText(oldVideoIndex, oldVideoName);
    }

    CString oldVideoName2;
    int oldVideoIndex2 = m_cmbVideo2.GetCurSel();

    if (oldVideoIndex2 != CB_ERR) {
        m_cmbVideo2.GetLBText(oldVideoIndex2, oldVideoName2);
    }

    // 刷新视频设备ComboBox
    m_CmbVideoDevices.ResetContent();
    m_cmbVideo2.ResetContent();
    lc_video_device_t videoDevice = { 0 };
    // 调用API, 得到视频设备数量
    int videos = lc_video_device_get_count();

    for (int i = 0; i < videos; i ++) {
        // 调用API, 得到第i个视频设备信息
        if (LC_OK == lc_video_device_get_device(i, &videoDevice)) {
            m_CmbVideoDevices.AddString(A2T(videoDevice.device_name));
            m_cmbVideo2.AddString(A2T(videoDevice.device_name));
        }
    }

    // 根据旧的视频设备，选择新的视频设备
    if (oldVideoName.IsEmpty() ||
            m_CmbVideoDevices.SelectString(-1, oldVideoName) < 0) {
        m_CmbVideoDevices.SetCurSel(0);
    }

    if (oldVideoName2.IsEmpty() ||
            m_cmbVideo2.SelectString(-1, oldVideoName2) < 0) {
        m_cmbVideo2.SetCurSel(0);
    }

    // 更新视频设备支持的视频格式
    UpdateVideoInfos();

    // 更新编码模板控件
    UpdatePresetsComboBox();
}

void CToolsDlg::UpdatePresetsComboBox() {
    USES_CONVERSION;

    //保存旧的模板名称
    CString oldPresetName;
    int oldPresetIndex = m_CmbPresets.GetCurSel();

    if (oldPresetIndex != CB_ERR) {
        m_CmbPresets.GetLBText(oldPresetIndex, oldPresetName);
    }

    // 刷新模板ComboBox
    m_CmbPresets.ResetContent();

    if (m_presets) {
        // 调用API, 释放原有的模板列表
        lc_list_free(m_presets);
        m_presets = NULL;
    }

    int iIndex = 0;

    if (m_bAuth) {
        // 调用API, 从服务端读取模板列表
        if (LC_OK == lc_transcode_preset_list(&m_presets)) {
            for (int i = 0 ; i < lc_list_count(m_presets); i++) {
                lc_transcode_preset_t* preset = (lc_transcode_preset_t*) lc_list_get_at(m_presets, i);

                // 过滤forward only模板，因为这种模板里不含编码参数
                if (preset->forward_only <= 0) {
                    // 过滤仅音频，仅视频模板
                    if (preset->audio.is_valid && preset->video.is_valid) {
                        iIndex = m_CmbPresets.AddString(A2T(preset->name));

                        if (iIndex >= 0) {
                            m_CmbPresets.SetItemData(iIndex, (DWORD_PTR) preset);
                        }
                    }
                }
            }
        } else {
            CString strMsg;
            strMsg.Format(_T("读取转码模板失败\r\n%s"), A2T(lc_get_last_error()));
            MessageBox(strMsg);
        }
    } else {

        const lc_transcode_preset_t* preset = g_GetUserPresets();

        while (preset->name[0]) {
            iIndex = m_CmbPresets.AddString(A2T(preset->name));

            if (iIndex >= 0) {
                m_CmbPresets.SetItemData(iIndex, (DWORD_PTR) preset);
            }

            preset ++;
        }
    }

    CString userEncoding;
    CString bitrate;
    FriendlyBitrate(bitrate,
                    m_Model.m_PresetUnion.m_sPreset.video.bitrate_in_bps +
                    m_Model.m_PresetUnion.m_sPreset.audio.bitrate_in_bps);

    userEncoding.Format(_T("自定义-%dx%d,%.2ffps,%s"),
                        m_Model.m_PresetUnion.m_sPreset.video.max_width_in_pixel,
                        m_Model.m_PresetUnion.m_sPreset.video.max_height_in_pixel,
                        m_Model.m_PresetUnion.m_sPreset.video.max_framerate_x100 / 100.0f,
                        (LPCTSTR)bitrate);

    iIndex = m_CmbPresets.AddString(userEncoding);

    if (iIndex >= 0) {
        m_CmbPresets.SetItemData(iIndex, (DWORD_PTR) &m_Model.m_PresetUnion.m_sPreset);
    }

    // 根据旧的模板名称，选择新的模板
    if (oldPresetName.IsEmpty() ||
            m_CmbPresets.SelectString(-1, oldPresetName) < 0) {
        m_CmbPresets.SetCurSel(0);
    }

    IfUserPreset();
}

void CToolsDlg::IfUserPreset() {
    int iSel = m_CmbPresets.GetCurSel();

    if (iSel >= 0) {
        if (m_CmbPresets.GetItemData(iSel) == (DWORD_PTR) &m_Model.m_PresetUnion.m_sPreset) {
            m_BtnUserPreset.ShowWindow(SW_SHOW);
        } else {
            m_BtnUserPreset.ShowWindow(SW_HIDE);
        }
    }
}

void CToolsDlg::OnPaint() {
    if (IsIconic()) {
        CPaintDC dc(this); // device context for painting

        SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

        // Center icon in client rectangle
        int cxIcon = GetSystemMetrics(SM_CXICON);
        int cyIcon = GetSystemMetrics(SM_CYICON);
        CRect rect;
        GetClientRect(&rect);
        int x = (rect.Width() - cxIcon + 1) / 2;
        int y = (rect.Height() - cyIcon + 1) / 2;

        // Draw the icon
        dc.DrawIcon(x, y, m_hIcon);
    } else {
        CDialog::OnPaint();
    }
}

HCURSOR CToolsDlg::OnQueryDragIcon() {
    return static_cast<HCURSOR>(m_hIcon);
}


void CToolsDlg::OnBnClickedButtonAutoPreset() {

}

// 选择录制文件存储路径
void CToolsDlg::OnBnClickedButtonSelectPath() {
    CFileDialog cfd(FALSE, _T("flv"), _T("live"), OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST,
                    _T("*.flv|*.flv|All|*||"), this);

    if (IDOK == cfd.DoModal()) {
        m_EdtLocalPath.SetWindowText(cfd.GetPathName());
    }
}

//关闭程序
void CToolsDlg::OnBnClickedButtonClose() {
    if (m_handle) {
        if (IDOK == MessageBox(_T("正在推流中，确定要退出吗？"), _T("确认"), MB_OKCANCEL)) {
            OnBnClickedButtonStop();
        } else {
            return;
        }
    }

    EndDialog(0);
    theApp.PostThreadMessage(WM_QUIT, 0, 0);
}

// 停止录制
void CToolsDlg::OnBnClickedButtonStop() {
    if (m_handle) {
        lc_stop(m_handle);
        lc_close(m_handle);
        m_handle = NULL;
        OnStop();
    }

    if (m_uTimer) {
        KillTimer(m_uTimer);
        m_uTimer = 0;
    }

    UpdateStatus();
}

// 更新录制状态
void CToolsDlg::UpdateStatus() {
    LC_STATUS status = LC_STATUS_READY;

    if (m_handle) {
        // 调用API, 得到采集对象状态
        status = lc_get_status(m_handle);
    }

    UpdateStatusUi(status, LC_OK);
}

void CToolsDlg::UpdateStatusUi(LC_STATUS status, LC_CODE code) {
    USES_CONVERSION;

    CString strStatusName;
    int running = 0;

    switch (status) {
    case LC_STATUS_STARTING:
        strStatusName = _T("启动中...");
        running = 1;
        break;

    case LC_STATUS_RUNNING:
        strStatusName = _T("运行中...");
        running = 1;
        break;

    case LC_STATUS_RETRYING:
        strStatusName = _T("重试中...");
        running = 1;
        break;

    case LC_STATUS_READY:
        strStatusName = _T("就绪");
        running = 0;
        break;

    case LC_STATUS_STOPPED:
        strStatusName = _T("已停止");
        running = 0;
        break;
    }

    m_staStatus.SetWindowText(strStatusName);
    m_edtErrorDetail.SetWindowText(A2T(lc_get_error_message(code)));

    m_BtnStart.EnableWindow(!running);
    m_BtnStop.EnableWindow(running);
}

// 采集对象状态回调
void LC_API status_callback(lc_handle_t handle, LC_STATUS status, LC_CODE msg_code,
                            void* user_data) {
    reinterpret_cast<CToolsDlg*>(user_data)->OnStatusChanged(status, msg_code);
}
void CToolsDlg::DockPlayer(CDialogPlayer* player) {

    CRect win;
    GetWindowRect(win);
    SetWindowPos(NULL, win.left, win.top, 1024, win.Height(), SWP_NOZORDER | SWP_NOREPOSITION);

    player->ModifyStyleEx(WS_EX_TOOLWINDOW | WS_EX_WINDOWEDGE, 0);
    player->ModifyStyle(WS_OVERLAPPEDWINDOW, WS_CHILD);
    player->SetParent(this);

    CRect rcClient;
    GetClientRect(rcClient);

    player->SetWindowPos(NULL, 520, 0, rcClient.Width() - 520, rcClient.Height(),
                         SWP_NOZORDER | SWP_FRAMECHANGED);
}

void CToolsDlg::UndockPlayer(CDialogPlayer* player) {
    player->SetParent(NULL);
    player->ModifyStyleEx(0, WS_EX_TOOLWINDOW | WS_EX_WINDOWEDGE);
    player->ModifyStyle(WS_CHILD, WS_OVERLAPPEDWINDOW, SWP_FRAMECHANGED);
    player->SetOwner(this);

    CRect win;
    GetWindowRect(win);

    MoveWindow(win.left, win.top, 520, win.Height());
}

LRESULT CToolsDlg::OnStatusChanged_WM(WPARAM wParam, LPARAM lParam) {
    UpdateStatusUi((LC_STATUS)lParam, (LC_CODE)wParam);
    return TRUE;
}

void CToolsDlg::OnStatusChanged(LC_STATUS status, LC_CODE code) {
    PostMessage(WM_USER_STATUS_CHANGED, code, status);
}

void LC_API CToolsDlg::sample_callback(lc_handle_t handle, LC_SAMPLE_TYPE type, void* sample,
                                       void* usr_data) {
    reinterpret_cast<CToolsDlg*>(usr_data)->OnSample(handle, type, sample);
}

void CToolsDlg::OnSample(lc_handle_t handle, LC_SAMPLE_TYPE type, void* sample) {

    if (handle == m_handle) {
        switch (type) {
        case LC_SAMPLE_RAW_VIDEO:
            m_wndPlayer.PushVideo((lc_sample_raw_video_t*)sample);
            break;

        case LC_SAMPLE_RAW_AUDIO:
            m_wndPlayer.PushAudio((lc_sample_raw_audio_t*)sample);
            break;

        default:
            break;
        }
    }
}

void CToolsDlg::OnBnClickedButtonStart() {
    USES_CONVERSION;

    if (m_handle) {
        lc_close(m_handle);
        m_handle = NULL;
    }

    UpdateStatus();
    UpdateData();

    // 判断用户输入是否合法
    if (ValidateData()) {

        // 保存用户配置
        m_Model.Serailize();
        lc_config_t config = {0};
        strncpy_s(config.video_inputs[0].device_name, T2A(m_Model.m_VideoDevice), -1);
        strncpy_s(config.audio_inputs[0].device_name, T2A(m_Model.m_AudioDevice), -1);


        int dispId = -1;

        if (IsDisplay(m_Model.m_VideoDevice, dispId)) {
            config.video_inputs[0].info.display_info = m_arDisplayInfo[0];
        } else {

            int iInfo = m_cmbVideoInfos.GetCurSel();

            if (iInfo) {
                const lc_video_info_t* video_info =
                    (const lc_video_info_t*)m_cmbVideoInfos.GetItemData(iInfo);

                if (video_info) {
                    config.video_inputs[0].info.video_info = *video_info;
                }
            }
        }

        if (m_Model.m_bTwoVideoSrc) {
            strncpy_s(config.video_inputs[1].device_name, T2A(m_Model.m_VideoDevice2), -1);

            int dispId = -1;

            if (IsDisplay(m_Model.m_VideoDevice2, dispId)) {
                config.video_inputs[1].info.display_info = m_arDisplayInfo[1];
            } else {
                int iInfo2 = m_cmbVideoInfo2.GetCurSel();

                if (iInfo2) {
                    const lc_video_info_t* video_info2 =
                        (const lc_video_info_t*)m_cmbVideoInfo2.GetItemData(iInfo2);

                    if (video_info2) {
                        config.video_inputs[1].info.video_info = *video_info2;
                    }
                }
            }

            if (m_Model.m_ptVideo.x != 0) {
                config.video_inputs[0].pixel_dst_x = m_Model.m_ptVideo.x;
            }

            if (m_Model.m_ptVideo.y != 0) {
                config.video_inputs[0].pixel_dst_y = m_Model.m_ptVideo.y;
            }

            if (m_Model.m_szVideo.cx != 0) {
                config.video_inputs[0].pixel_width = m_Model.m_szVideo.cx;
            }

            if (m_Model.m_szVideo.cy != 0) {
                config.video_inputs[0].pixel_height = m_Model.m_szVideo.cy;
            }


            if (m_Model.m_ptVideo2.x != 0) {
                config.video_inputs[1].pixel_dst_x = m_Model.m_ptVideo2.x;
            }

            if (m_Model.m_ptVideo2.y != 0) {
                config.video_inputs[1].pixel_dst_y = m_Model.m_ptVideo2.y;
            }

            if (m_Model.m_szVideo2.cx != 0) {
                config.video_inputs[1].pixel_width = m_Model.m_szVideo2.cx;
            }

            if (m_Model.m_szVideo2.cy != 0) {
                config.video_inputs[1].pixel_height = m_Model.m_szVideo2.cy;
            }
        }

        int iPresetIndex = m_CmbPresets.GetCurSel();
        lc_transcode_preset_t* preset = (lc_transcode_preset_t*)m_CmbPresets.GetItemData(iPresetIndex);

        config.encoding.video = preset->video;
        config.encoding.audio = preset->audio;

        LC_CODE ret = LC_OK;
        // 调用API, 创建采集对象
        m_handle = lc_create(&config, &ret);

        if (!m_handle) {
            MessageBox(_T("创建采集对象失败"), _T("失败"), MB_OK);
            return ;
        } else {
            // 调用API, 注册采集对象状态回调函数
            lc_register_status_callback(m_handle, status_callback, this);
            lc_sample_set_callback(m_handle, sample_callback, LC_SAMPLE_RAW_AUDIO | LC_SAMPLE_RAW_VIDEO, this);
            m_wndPlayer.Reset();

            // 调用API, 启动采集
            ret = lc_start(m_handle,
                           m_Model.m_RtmpOption == EXISTING_SESSION ? T2A(m_Model.m_BCESessionId) : NULL,
                           m_Model.m_RtmpOption == USER_RTMP ? T2A(m_Model.m_UserRTMPUrl) : NULL,
                           m_Model.m_SaveLocal ? T2A(m_Model.m_LocalSavePath) : NULL);

            if (ret == LC_OK) {
                // 启动一个Timer,用于更新状态，播放地址和码率信息
                m_uTimer = SetTimer(0x1, 1000, NULL);

                if (m_uTimer == 0) {
                    CLogMgr::Instance().AppendLog(LC_LOG_ERROR, _T("set timer failed. play url will not be updated."));
                }

                OnStart();
            }
        }

        UpdateStatus();
    }
}


void CToolsDlg::OnOK() {

}

void CToolsDlg::OnCancel() {

}

void CToolsDlg::OnBnClickedRadioExistingSession() {
    int nRtmpOption = GetCheckedRadioButton(IDC_RADIO_EXISTING_SESSION, IDC_RADIO_RTMP) -
                      IDC_RADIO_EXISTING_SESSION;
    UpdateRtmpRadio(nRtmpOption);
}

void CToolsDlg::OnBnClickedRadioRtmp() {
    int nRtmpOption = GetCheckedRadioButton(IDC_RADIO_EXISTING_SESSION, IDC_RADIO_RTMP) -
                      IDC_RADIO_EXISTING_SESSION;
    UpdateRtmpRadio(nRtmpOption);
}

void CToolsDlg::OnBnClickedRadioBce() {
    int nRtmpOption = GetCheckedRadioButton(IDC_RADIO_EXISTING_SESSION, IDC_RADIO_RTMP) -
                      IDC_RADIO_EXISTING_SESSION;
    UpdateRtmpRadio(nRtmpOption);
}

void CToolsDlg::UpdateRtmpRadio(int nRtmpOption) {
    CWnd* userRtmpCtls[] = {&m_EdtUserRtmp, 0};
    CWnd* existingSessionCtls[] = {&m_edtExistingSession, &m_btnSelectSession, 0};

    for (int i = 0; userRtmpCtls[i]; i ++) {
        userRtmpCtls[i]->EnableWindow(nRtmpOption == USER_RTMP);
    }

    for (int i = 0; existingSessionCtls[i]; i ++) {
        existingSessionCtls[i]->EnableWindow(nRtmpOption == EXISTING_SESSION);
    }
}


void CToolsDlg::OnBnClickedCheckSaveLocal() {
    BOOL bCheck = m_ChkSaveLocal.GetCheck() && m_ChkSaveLocal.IsWindowEnabled();

    CWnd* ctrls[] = {&m_EdtLocalPath, &m_BtnSelectPath, 0};

    for (int i = 0 ; ctrls[i]; i ++) {
        ctrls[i]->EnableWindow(bCheck);
    }
}

void CToolsDlg::OnBnClickedButtonRefreshData() {
    UpdateLiveCaptureData();
}

void CToolsDlg::OnBnClickedUserPreset() {
    CDialogUserPreset dlg(&m_Model.m_PresetUnion.m_sPreset, this);

    if (IDOK == dlg.DoModal()) {
        int iSel = m_CmbPresets.GetCurSel();

        if (iSel >= 0) {
            if (m_CmbPresets.GetItemData(iSel) == (DWORD_PTR) &m_Model.m_PresetUnion.m_sPreset) {

                m_CmbPresets.DeleteString(iSel);

                CString userEncoding;
                CString bitrate;
                FriendlyBitrate(bitrate,
                                m_Model.m_PresetUnion.m_sPreset.video.bitrate_in_bps +
                                m_Model.m_PresetUnion.m_sPreset.audio.bitrate_in_bps);

                userEncoding.Format(_T("自定义-%dx%d,%.2ffps,%s"),
                                    m_Model.m_PresetUnion.m_sPreset.video.max_width_in_pixel,
                                    m_Model.m_PresetUnion.m_sPreset.video.max_height_in_pixel,
                                    m_Model.m_PresetUnion.m_sPreset.video.max_framerate_x100 / 100.0f,
                                    (LPCTSTR)bitrate);

                int iNewPreset = m_CmbPresets.AddString(userEncoding);

                if (iNewPreset >= 0) {
                    m_CmbPresets.SetItemData(iNewPreset, (DWORD_PTR) &m_Model.m_PresetUnion.m_sPreset);
                    m_CmbPresets.SetCurSel(iNewPreset);
                }
            }
        }
    }
}

void CToolsDlg::OnTimer(UINT_PTR nIDEvent) {
    USES_CONVERSION;

    if (m_handle) {

        lc_session_play_t play = { 0 };

        // 调用API, 得到播放地址
        if (LC_OK == lc_query_play_url(m_handle, &play)) {
            CString text;
            m_EdtHLSPlayUrl.GetWindowText(text);

            if (text.CompareNoCase(A2T(play.hls_url)) != 0) {
                m_EdtHLSPlayUrl.SetWindowText(A2T(play.hls_url));
            }

            m_EdtRtmpPlayUrl.GetWindowText(text);

            if (text.CompareNoCase(A2T(play.rtmp_url)) != 0) {
                m_EdtRtmpPlayUrl.SetWindowText(A2T(play.rtmp_url));
            }
        }

        lc_statistics_stream_t strm = {0 };

        // 调用API, 得到当前的码率信息
        if (LC_OK == lc_statistics_get(m_handle, &strm)) {
            CString strStatis;
            CString strBitrate;
            FriendlyBitrate(strBitrate, strm.bitrate_in_bps);
            strStatis.Format(_T("码率:%s, 帧率:%.2f fps"), (LPCTSTR)strBitrate,
                             strm.video.framerate_x100 / 100.0f);

            m_edtStatis.SetWindowText(strStatis);
        }
    }
}

BOOL CToolsDlg::ValidateData() {
    CString err;

    if (!m_Model.Validate(err)) {
        MessageBox(err, _T("错误"), MB_OK);
        return FALSE;
    }

    return TRUE;
}
void CToolsDlg::OnMenuPreset() {
    if (!m_pPresetDlg) {
        m_pPresetDlg = new CPresetDlg(this);
        m_pPresetDlg->Create(CPresetDlg::IDD, this);

        if (m_pPresetDlg->GetSafeHwnd()) {
            m_pPresetDlg->ShowWindow(SW_SHOW);
        } else {
            MessageBox(_T("创建模板窗口失败"), _T("错误"), MB_OK | MB_ICONERROR);
        }
    } else {
        m_pPresetDlg->ShowWindow(SW_SHOW);
    }
}

void CToolsDlg::OnUpdateMenuPreset(CCmdUI* pCmdUI) {
    if (m_handle || (!m_bAuth) || (m_pPresetDlg && IsWindow(m_pPresetDlg->GetSafeHwnd())
                                   && m_pPresetDlg->IsWindowVisible())) {
        pCmdUI->Enable(FALSE);
    } else {
        pCmdUI->Enable(TRUE);
    }
}

void CToolsDlg::OnMenuSession() {
    if (!m_pSessionDlg) {
        m_pSessionDlg = new CSessionDlg(this);
        m_pSessionDlg->Create(CSessionDlg::IDD, this);

        if (m_pSessionDlg->GetSafeHwnd()) {
            m_pSessionDlg->ShowWindow(SW_SHOW);
        } else {
            MessageBox(_T("创建会话窗口失败"), _T("错误"), MB_OK | MB_ICONERROR);
        }
    } else {
        m_pSessionDlg->ShowWindow(SW_SHOW);
    }
}

void CToolsDlg::OnUpdateMenuSession(CCmdUI* pCmdUI) {
    if (m_handle || (!m_bAuth) || (m_pSessionDlg && m_pSessionDlg->GetSafeHwnd()
                                   && m_pSessionDlg->IsWindowVisible())) {
        pCmdUI->Enable(FALSE);
    } else {
        pCmdUI->Enable(TRUE);
    }

}

void CToolsDlg::OnMenuLog() {
    if (!m_pLogDlg) {
        m_pLogDlg = new CLogDlg(this);
        m_pLogDlg->Create(CLogDlg::IDD, this);

        if (m_pLogDlg->GetSafeHwnd()) {
            m_pLogDlg->ShowWindow(SW_SHOW);
        } else {
            MessageBox(_T("创建日志窗口失败"), _T("错误"), MB_OK | MB_ICONERROR);
        }
    } else {
        m_pLogDlg->ShowWindow(SW_SHOW);
    }
}

void CToolsDlg::OnUpdateMenuLog(CCmdUI* pCmdUI) {
    if (m_pLogDlg) {
        pCmdUI->Enable(FALSE);
    } else {
        pCmdUI->Enable(TRUE);
    }
}
void CToolsDlg::OnLogDlgClosed(CLogDlg* dlg) {
    if (dlg == m_pLogDlg) {
        delete m_pLogDlg ;
        m_pLogDlg = NULL;
        OnIdleUpdateCmdUI(0, 0);
        DrawMenuBar();
        BringWindowToTop();
    }
}
void CToolsDlg::OnPresetDlgClosed(CPresetDlg* dlg) {
    if (dlg == m_pPresetDlg) {
        delete m_pPresetDlg;
        m_pPresetDlg = NULL;
        OnIdleUpdateCmdUI(0, 0);
        DrawMenuBar();
        BringWindowToTop();
    }
}
void CToolsDlg::OnSessionDlgClosed(CSessionDlg* dlg) {
    if (dlg == m_pSessionDlg) {
        delete m_pSessionDlg;
        m_pSessionDlg = NULL;
        OnIdleUpdateCmdUI(0, 0);
        DrawMenuBar();
        BringWindowToTop();
    }
}

void CToolsDlg::OnNotificationDlgClosed(CDialogNotification* dlg) {
    if (dlg == m_pNotificationDlg) {
        delete m_pNotificationDlg;
        m_pNotificationDlg = NULL;
        OnIdleUpdateCmdUI(0, 0);
        DrawMenuBar();
        BringWindowToTop();
    }
}

void CToolsDlg::OnOptionDlgClosed(CDialogOption* dlg) {
    if (dlg == m_pOptionDlg) {
        delete m_pOptionDlg;
        m_pOptionDlg = NULL;
        OnIdleUpdateCmdUI(0, 0);
        DrawMenuBar();
        BringWindowToTop();
    }
}
void CToolsDlg::EnableUI(BOOL enable) {
    CWnd* ctrls[] = {
        &m_BtnSelectPath,
        &m_BtnUserPreset,
        &m_CmbVideoDevices,
        &m_cmbVideoInfos,
        &m_btnVideoConfig,
        &m_CmbAudioDevices,
        &m_CmbPresets,
        &m_ChkSaveLocal,
        &m_EdtLocalPath,
        &m_EdtUserRtmp,
        &m_rdoUserRtmp,
        &m_rdoExistRtmp,
        &m_edtExistingSession,
        &m_btnSelectSession,
        &m_btnRefresh,
        &m_chkVideo2,
        &m_cmbVideo2,
        &m_cmbVideoInfo2,
        &m_btnVideoConfig2,
        &m_edtVideoX,
        &m_edtVideoY,
        &m_edtVideoWidth,
        &m_edtVideoHeight,
        &m_edtVideo2X,
        &m_edtVideo2Y,
        &m_edtVideo2Width,
        &m_edtVideo2Height,
        NULL
    };

    CWnd** curWnd = ctrls;

    while (*curWnd) {
        (*curWnd)->EnableWindow(enable);
        curWnd++;
    }

    UpdateAuthMode();

    if (enable) {
        OnBnClickedCheckSaveLocal();
        OnBnClickedRadioRtmp();
        OnBnClickedCheckVideo2();
    }
}

LRESULT CToolsDlg::OnIdleUpdateCmdUI(WPARAM, LPARAM) {
    CMenu* pMenu = GetMenu();
    CCmdUI state;
    state.m_pMenu = pMenu;
    state.m_nIndexMax = pMenu->GetMenuItemCount();

    for (state.m_nIndex = 0; state.m_nIndex < state.m_nIndexMax;
            state.m_nIndex++) {
        state.m_nID = pMenu->GetMenuItemID(state.m_nIndex);

        if (state.m_nID == 0) {
            continue;    // menu separator or invalid cmd - ignore it
        }

        ASSERT(state.m_pOther == NULL);
        ASSERT(state.m_pMenu != NULL);

        if (state.m_nID == (UINT) - 1) {
            // possibly a popup menu, route to first item of that popup
            state.m_pSubMenu = pMenu->GetSubMenu(state.m_nIndex);

            if (state.m_pSubMenu == NULL ||
                    (state.m_nID = state.m_pSubMenu->GetMenuItemID(0)) == 0 ||
                    state.m_nID == (UINT) - 1) {
                continue;       // first item of popup can't be routed to
            }

            state.DoUpdate(this, FALSE);    // popups are never auto disabled
        } else {
            // normal menu item
            // Auto enable/disable if frame window has 'm_bAutoMenuEnable'
            //    set and command is _not_ a system command.
            state.m_pSubMenu = NULL;
            state.DoUpdate(this, state.m_nID < 0xF000);
        }

        // adjust for menu deletions and additions
        UINT nCount = pMenu->GetMenuItemCount();

        if (nCount < state.m_nIndexMax) {
            state.m_nIndex -= (state.m_nIndexMax - nCount);

            while (state.m_nIndex < nCount &&
                    pMenu->GetMenuItemID(state.m_nIndex) == state.m_nID) {
                state.m_nIndex++;
            }
        }

        state.m_nIndexMax = nCount;
    }

    return 0;
}

void CToolsDlg::UpdateAuthMode() {
    m_rdoExistRtmp.EnableWindow(m_bAuth);
    CheckRadioButton(IDC_RADIO_EXISTING_SESSION, IDC_RADIO_RTMP,
                     m_bAuth ? IDC_RADIO_EXISTING_SESSION : IDC_RADIO_RTMP);
    UpdateRtmpRadio(EXISTING_SESSION);
}

void CToolsDlg::OnStart() {
    EnableUI(FALSE);

    CWnd** dlgs[] = {(CWnd**)& m_pOptionDlg, (CWnd**)& m_pPresetDlg, (CWnd**)& m_pSessionDlg, (CWnd**)& m_pNotificationDlg, NULL};

    CWnd** * ppdlg = &dlgs[0];

    while (*ppdlg) {
        CWnd** pdlg = *ppdlg;

        if (IsWindow((*pdlg)->GetSafeHwnd())) {
            (*pdlg)->DestroyWindow();
        }

        delete *pdlg;
        *pdlg = NULL;

        ppdlg++;
    }

    OnIdleUpdateCmdUI(0, 0);
    DrawMenuBar();
}

void CToolsDlg::OnStop() {
    EnableUI(TRUE);
    OnIdleUpdateCmdUI(0, 0);
    DrawMenuBar();
}
void CToolsDlg::OnDestroy() {
    OnBnClickedButtonStop();
    m_wndPlayer.DestroyWindow();
    CDialog::OnDestroy();
}

void CToolsDlg::OnBnClickedButtonSelectSession() {
    CSessionDlg dlg(this, TRUE);
    dlg.DoModal();
    CString id = dlg.GetSelectedSessionId();

    if (!id.IsEmpty()) {
        m_edtExistingSession.SetWindowText(id);
    }
}

void CToolsDlg::OnCbnSelchangeComboVideoInfo() {

}
void CToolsDlg::OnCbnSelchangeComboPreset() {
    IfUserPreset();
}

LC_CODE LC_API CToolsDlg::enumVideoInfo(const lc_video_info_t* info, void* inst) {
    reinterpret_cast<std::vector<lc_video_info_t>*>(inst)->push_back(*info);
    return LC_OK;
}

LC_CODE CToolsDlg::OnEnumVideoInfo(const lc_video_info_t* info) {
    m_vecVideoInfos.push_back(*info);
    return LC_OK;
}

void CToolsDlg::UpdateVideoInfos() {
    USES_CONVERSION;

    int cur = m_CmbVideoDevices.GetCurSel();
    m_cmbVideoInfos.ResetContent();
    m_vecVideoInfos.clear();

    if (cur >= 0) {
        CString devName;
        m_CmbVideoDevices.GetLBText(cur, devName);

        int dispId;

        if (IsDisplay(devName, dispId)) {
            m_cmbVideoInfos.ShowWindow(SW_HIDE);
            m_btnVideoConfig.ShowWindow(SW_SHOW);
        } else {
            m_cmbVideoInfos.ShowWindow(SW_SHOW);
            m_btnVideoConfig.ShowWindow(SW_HIDE);

            // 调用API, 读取指定视频设备的支持的视频格式
            lc_video_device_enum_video_info(T2A(devName), enumVideoInfo, &m_vecVideoInfos);

            if (m_vecVideoInfos.size() > 0) {
                std::sort(m_vecVideoInfos.begin(), m_vecVideoInfos.end(), VideoInfoPred());

                for (std::vector<lc_video_info_t>::const_iterator it = m_vecVideoInfos.begin();
                        it != m_vecVideoInfos.end();
                        it ++) {
                    int id = m_cmbVideoInfos.AddString(video_info_string(*it));

                    if (id >= 0) {
                        m_cmbVideoInfos.SetItemData(id, (DWORD_PTR) & (*it));
                    }
                }
            }
        }
    }

    int id = m_cmbVideoInfos.AddString(_T("默认"));

    if (id >= 0) {
        m_cmbVideoInfos.SetCurSel(id);
    }

    cur = m_cmbVideo2.GetCurSel();
    m_cmbVideoInfo2.ResetContent();
    m_vecVideoInfos2.clear();

    if (cur >= 0) {
        CString devName;
        m_cmbVideo2.GetLBText(cur, devName);
        int dispId;

        if (IsDisplay(devName, dispId)) {
            m_cmbVideoInfo2.ShowWindow(SW_HIDE);
            m_btnVideoConfig2.ShowWindow(SW_SHOW);
        } else {
            m_cmbVideoInfo2.ShowWindow(SW_SHOW);
            m_btnVideoConfig2.ShowWindow(SW_HIDE);

            // 调用API, 读取指定视频设备的支持的视频格式
            lc_video_device_enum_video_info(T2A(devName), enumVideoInfo, &m_vecVideoInfos2);

            if (m_vecVideoInfos2.size() > 0) {
                std::sort(m_vecVideoInfos2.begin(), m_vecVideoInfos2.end(), VideoInfoPred());

                for (std::vector<lc_video_info_t>::const_iterator it = m_vecVideoInfos2.begin();
                        it != m_vecVideoInfos2.end();
                        it ++) {
                    int id = m_cmbVideoInfo2.AddString(video_info_string(*it));

                    if (id >= 0) {
                        m_cmbVideoInfo2.SetItemData(id, (DWORD_PTR) & (*it));
                    }
                }
            }
        }
    }

    id = m_cmbVideoInfo2.AddString(_T("默认"));

    if (id >= 0) {
        m_cmbVideoInfo2.SetCurSel(id);
    }
}

void CToolsDlg::OnCbnSelchangeComboVideo() {
    UpdateVideoInfos();


}

void CToolsDlg::OnCbnSelchangeComboVideo2() {
    UpdateVideoInfos();
}

int CToolsDlg::OnCreate(LPCREATESTRUCT lpCreateStruct) {
    if (CDialog::OnCreate(lpCreateStruct) == -1) {
        return -1;
    }

    if (!m_wndPlayer.Create(CDialogPlayer::IDD, this)) {
        return -1;
    }

    m_wndPlayer.SetHost(this);

    DockPlayer(&m_wndPlayer);
    m_wndPlayer.ShowWindow(SW_SHOW);
    return 0;
}

void CToolsDlg::OnNotification() {
    if (!m_pNotificationDlg) {
        m_pNotificationDlg = new CDialogNotification(this);
        m_pNotificationDlg->Create(CDialogNotification::IDD, this);

        if (m_pNotificationDlg->GetSafeHwnd()) {
            m_pNotificationDlg->ShowWindow(SW_SHOW);
        } else {
            MessageBox(_T("创建会话窗口失败"), _T("错误"), MB_OK | MB_ICONERROR);
        }
    } else {
        m_pNotificationDlg->ShowWindow(SW_SHOW);
    }
}

void CToolsDlg::OnOption() {
    if (!m_pOptionDlg) {
        m_pOptionDlg = new CDialogOption(this);
        m_pOptionDlg->Create(CDialogOption::IDD, this);

        if (m_pOptionDlg->GetSafeHwnd()) {
            m_pOptionDlg->ShowWindow(SW_SHOW);
        } else {
            MessageBox(_T("创建会话窗口失败"), _T("错误"), MB_OK | MB_ICONERROR);
        }
    } else {
        m_pOptionDlg->ShowWindow(SW_SHOW);
    }
}

void CToolsDlg::OnUpdateNotification(CCmdUI* pCmdUI) {
    if (m_handle || (!m_bAuth) || (m_pNotificationDlg && IsWindow(m_pNotificationDlg->GetSafeHwnd())
                                   && m_pNotificationDlg->IsWindowVisible())) {
        pCmdUI->Enable(FALSE);
    } else {
        pCmdUI->Enable(TRUE);
    }
}

void CToolsDlg::OnUpdateOption(CCmdUI* pCmdUI) {
    if (m_handle || (m_pOptionDlg && IsWindow(m_pOptionDlg->GetSafeHwnd())
                     && m_pOptionDlg->IsWindowVisible())) {
        pCmdUI->Enable(FALSE);
    } else {
        pCmdUI->Enable(TRUE);
    }
}

void CToolsDlg::OnBnClickedCheckVideo2() {
    BOOL bCheck = m_chkVideo2.GetCheck() && m_chkVideo2.IsWindowEnabled();

    CWnd* ctrls[] = {
        &m_cmbVideo2,
        &m_cmbVideoInfo2,
        &m_btnVideoConfig2,
        &m_edtVideoX,
        &m_edtVideoY,
        &m_edtVideoWidth,
        &m_edtVideoHeight,
        &m_edtVideo2X,
        &m_edtVideo2Y,
        &m_edtVideo2Width,
        &m_edtVideo2Height,
        NULL
    };

    CWnd** idx = &ctrls[0];

    while (*idx) {
        (*idx)->EnableWindow(bCheck);
        idx ++;
    }
}

void CToolsDlg::OnBnClickedButtonVideoConfig() {
    CDisplayConfigDlg dlg(&m_arDisplayInfo[0], this);
    dlg.DoModal();
}

void CToolsDlg::OnBnClickedButtonVideoConfig2() {
    CDisplayConfigDlg dlg(&m_arDisplayInfo[1], this);
    dlg.DoModal();
}

void CToolsDlg::OnSysCommand(UINT nID, LPARAM lParam) {
    if (nID == SC_CLOSE) {
        OnBnClickedButtonClose();
        return;
    }

    CDialog::OnSysCommand(nID, lParam);
}
