#include <otomarket/license/LicenseActivationUi.h>

#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>

namespace otomarket::license {
namespace {

bool isLicensedLike(LicenseState state) {
  return state == LicenseState::Licensed ||
         state == LicenseState::LicensedOfflineGrace ||
         state == LicenseState::VerifyDue;
}

std::string joinLines(const std::vector<std::string>& lines) {
  std::ostringstream output;

  for (size_t index = 0; index < lines.size(); ++index) {
    if (index != 0) {
      output << "\n";
    }
    output << lines[index];
  }

  return output.str();
}

std::string statusTextForState(LicenseState state, const LicenseActivationStrings& strings) {
  switch (state) {
    case LicenseState::Unknown:
      return strings.unknownStatus;
    case LicenseState::Unlicensed:
      return strings.unlicensedStatus;
    case LicenseState::Licensed:
      return strings.licensedStatus;
    case LicenseState::LicensedOfflineGrace:
      return strings.offlineGraceStatus;
    case LicenseState::VerifyDue:
      return strings.verifyDueStatus;
    case LicenseState::Expired:
      return strings.expiredStatus;
    case LicenseState::Revoked:
      return strings.revokedStatus;
    case LicenseState::NotFound:
      return strings.notFoundStatus;
    case LicenseState::ProductMismatch:
      return strings.productMismatchStatus;
    case LicenseState::SeatLimit:
      return strings.seatLimitStatus;
    case LicenseState::InvalidCache:
      return strings.invalidCacheStatus;
    case LicenseState::NetworkUnavailable:
      return strings.networkUnavailableStatus;
    case LicenseState::Error:
      return strings.errorStatus;
  }

  return strings.unknownStatus;
}

std::optional<int> resolvedMaxActivations(const LicenseActivationViewInput& input) {
  if (input.maxActivations) {
    return input.maxActivations;
  }

  if (input.license && input.license->maxActivations > 0) {
    return input.license->maxActivations;
  }

  return std::nullopt;
}

std::optional<std::chrono::system_clock::time_point> resolvedExpiresAt(
  const LicenseActivationViewInput& input
) {
  if (input.expiresAt) {
    return input.expiresAt;
  }

  if (input.license && input.license->expiresAt) {
    return input.license->expiresAt;
  }

  return std::nullopt;
}

bool shouldShowNoExpiration(const LicenseActivationViewInput& input) {
  return isLicensedLike(input.state) || input.license.has_value();
}

} // namespace

LicenseActivationStrings englishLicenseActivationStrings() {
  LicenseActivationStrings strings;
  strings.trialButton = "Start free trial";
  return strings;
}

LicenseActivationStrings japaneseLicenseActivationStrings() {
  LicenseActivationStrings strings;
  strings.licenseKeyLabel = "ライセンスキー";
  strings.licenseKeyPlaceholder = "ライセンスキーを入力";
  strings.activateButton = "認証";
  strings.deactivateButton = "解除";
  strings.trialButton = "無料でためす";
  strings.checkingStatus = "ライセンス状態を確認中...";
  strings.activatingStatus = "認証中...";
  strings.deactivatingStatus = "解除中...";
  strings.unknownStatus = "ライセンス状態は未確認です";
  strings.unlicensedStatus = "未認証";
  strings.licensedStatus = "認証済み";
  strings.offlineGraceStatus = "認証済み（オフライン猶予中）";
  strings.verifyDueStatus = "オンライン確認が必要です";
  strings.expiredStatus = "期限切れ";
  strings.revokedStatus = "失効済み";
  strings.notFoundStatus = "ライセンスが見つかりません";
  strings.productMismatchStatus = "製品が一致しません";
  strings.seatLimitStatus = "認証台数の上限に達しています";
  strings.invalidCacheStatus = "保存済みライセンスが無効です";
  strings.networkUnavailableStatus = "ライセンスサーバーに接続できません";
  strings.errorStatus = "ライセンスエラー";
  strings.seatsLabel = "利用台数";
  strings.maxActivationsLabel = "最大認証台数";
  strings.expiresAtLabel = "有効期限";
  strings.noExpirationText = "有効期限なし";
  strings.offlineGraceDetail = "保存済みライセンスをオフライン猶予期間内で使用しています。";
  strings.errorSeatLimit = "このライセンスは認証台数の上限に達しています。";
  strings.errorExpired = "このライセンスは期限切れです。";
  strings.errorRevoked = "このライセンスは失効しています。";
  strings.errorNotFound = "ライセンスキーが見つかりません。";
  strings.errorProductMismatch = "このライセンスは別の製品用です。";
  strings.errorNetwork = "ライセンスサーバーに接続できません。通信状態を確認して再試行してください。";
  strings.errorRequestRejected = "ライセンスサーバーがリクエストを拒否しました。";
  strings.errorInvalidResponse = "ライセンスサーバーの応答が不正です。";
  strings.errorInvalidSignature = "保存済みライセンスの署名が不正です。";
  strings.errorInvalidCache = "保存済みライセンスファイルが不正です。";
  strings.errorCacheIo = "ライセンスファイルの読み書きに失敗しました。";
  strings.errorCacheMissing = "保存済みライセンスがありません。";
  strings.errorMachineMismatch = "このライセンスは別のマシンで認証されています。";
  strings.errorConfig = "License SDK の設定が不足しています。";
  strings.errorFallback = "ライセンス処理に失敗しました。";
  return strings;
}

// Machine-translated defaults, pending native review.
// 機械翻訳・ネイティブ校正待ち。
LicenseActivationStrings germanLicenseActivationStrings() {
  LicenseActivationStrings strings;
  strings.licenseKeyLabel = "Lizenzschlüssel";
  strings.licenseKeyPlaceholder = "Lizenzschlüssel eingeben";
  strings.activateButton = "Aktivieren";
  strings.deactivateButton = "Deaktivieren";
  strings.trialButton = "Kostenlos testen";
  strings.checkingStatus = "Lizenzstatus wird geprüft...";
  strings.activatingStatus = "Aktivierung läuft...";
  strings.deactivatingStatus = "Deaktivierung läuft...";
  strings.unknownStatus = "Lizenzstatus unbekannt";
  strings.unlicensedStatus = "Nicht lizenziert";
  strings.licensedStatus = "Lizenziert";
  strings.offlineGraceStatus = "Lizenziert (Offline-Kulanzzeit)";
  strings.verifyDueStatus = "Online-Verifizierung erforderlich";
  strings.expiredStatus = "Abgelaufen";
  strings.revokedStatus = "Widerrufen";
  strings.notFoundStatus = "Lizenz nicht gefunden";
  strings.productMismatchStatus = "Produkt stimmt nicht überein";
  strings.seatLimitStatus = "Aktivierungslimit erreicht";
  strings.invalidCacheStatus = "Gespeicherte Lizenz ist ungültig";
  strings.networkUnavailableStatus = "Lizenzserver nicht erreichbar";
  strings.errorStatus = "Lizenzfehler";
  strings.seatsLabel = "Plätze";
  strings.maxActivationsLabel = "Maximale Aktivierungen";
  strings.expiresAtLabel = "Läuft ab";
  strings.noExpirationText = "Kein Ablaufdatum";
  strings.offlineGraceDetail =
    "Die gespeicherte Lizenz wird während der Offline-Kulanzzeit verwendet.";
  strings.errorSeatLimit = "Diese Lizenz hat ihr Aktivierungslimit erreicht.";
  strings.errorExpired = "Diese Lizenz ist abgelaufen.";
  strings.errorRevoked = "Diese Lizenz wurde widerrufen.";
  strings.errorNotFound = "Der Lizenzschlüssel wurde nicht gefunden.";
  strings.errorProductMismatch = "Diese Lizenz ist für ein anderes Produkt.";
  strings.errorNetwork =
    "Der Lizenzserver konnte nicht erreicht werden. Prüfen Sie Ihre Verbindung und versuchen Sie es erneut.";
  strings.errorRequestRejected = "Der Lizenzserver hat die Anfrage abgelehnt.";
  strings.errorInvalidResponse = "Der Lizenzserver hat eine ungültige Antwort zurückgegeben.";
  strings.errorInvalidSignature = "Die Signatur der gespeicherten Lizenz ist ungültig.";
  strings.errorInvalidCache = "Die gespeicherte Lizenzdatei ist ungültig.";
  strings.errorCacheIo =
    "Die gespeicherte Lizenzdatei konnte nicht gelesen oder geschrieben werden.";
  strings.errorCacheMissing = "Es wurde keine gespeicherte Lizenz gefunden.";
  strings.errorMachineMismatch = "Diese Lizenz wurde auf einem anderen Computer aktiviert.";
  strings.errorConfig = "Die Konfiguration des License SDK ist unvollständig.";
  strings.errorFallback = "Der Lizenzvorgang ist fehlgeschlagen.";
  return strings;
}

LicenseActivationStrings spanishLicenseActivationStrings() {
  LicenseActivationStrings strings;
  strings.licenseKeyLabel = "Clave de licencia";
  strings.licenseKeyPlaceholder = "Introduce la clave de licencia";
  strings.activateButton = "Activar";
  strings.deactivateButton = "Desactivar";
  strings.trialButton = "Probar gratis";
  strings.checkingStatus = "Comprobando licencia...";
  strings.activatingStatus = "Activando...";
  strings.deactivatingStatus = "Desactivando...";
  strings.unknownStatus = "Estado de licencia desconocido";
  strings.unlicensedStatus = "Sin licencia";
  strings.licensedStatus = "Con licencia";
  strings.offlineGraceStatus = "Con licencia (periodo de gracia sin conexión)";
  strings.verifyDueStatus = "Se requiere verificación en línea";
  strings.expiredStatus = "Caducada";
  strings.revokedStatus = "Revocada";
  strings.notFoundStatus = "Licencia no encontrada";
  strings.productMismatchStatus = "Producto no coincidente";
  strings.seatLimitStatus = "Límite de activaciones alcanzado";
  strings.invalidCacheStatus = "La licencia guardada no es válida";
  strings.networkUnavailableStatus = "Servidor de licencias no disponible";
  strings.errorStatus = "Error de licencia";
  strings.seatsLabel = "Puestos";
  strings.maxActivationsLabel = "Activaciones máximas";
  strings.expiresAtLabel = "Caduca";
  strings.noExpirationText = "Sin caducidad";
  strings.offlineGraceDetail =
    "Usando la licencia guardada durante el periodo de gracia para reintento sin conexión.";
  strings.errorSeatLimit = "Esta licencia ha alcanzado su límite de activaciones.";
  strings.errorExpired = "Esta licencia ha caducado.";
  strings.errorRevoked = "Esta licencia ha sido revocada.";
  strings.errorNotFound = "No se encontró la clave de licencia.";
  strings.errorProductMismatch = "Esta licencia es para un producto diferente.";
  strings.errorNetwork =
    "No se pudo conectar con el servidor de licencias. Revisa tu conexión e inténtalo de nuevo.";
  strings.errorRequestRejected = "El servidor de licencias rechazó la solicitud.";
  strings.errorInvalidResponse = "El servidor de licencias devolvió una respuesta no válida.";
  strings.errorInvalidSignature = "La firma de la licencia guardada no es válida.";
  strings.errorInvalidCache = "El archivo de licencia guardado no es válido.";
  strings.errorCacheIo = "No se pudo leer ni escribir el archivo de licencia guardado.";
  strings.errorCacheMissing = "No se encontró ninguna licencia guardada.";
  strings.errorMachineMismatch = "Esta licencia se activó en otro equipo.";
  strings.errorConfig = "La configuración del License SDK está incompleta.";
  strings.errorFallback = "La operación de licencia falló.";
  return strings;
}

LicenseActivationStrings brazilianPortugueseLicenseActivationStrings() {
  LicenseActivationStrings strings;
  strings.licenseKeyLabel = "Chave de licença";
  strings.licenseKeyPlaceholder = "Insira a chave de licença";
  strings.activateButton = "Ativar";
  strings.deactivateButton = "Desativar";
  strings.trialButton = "Testar grátis";
  strings.checkingStatus = "Verificando licença...";
  strings.activatingStatus = "Ativando...";
  strings.deactivatingStatus = "Desativando...";
  strings.unknownStatus = "Status da licença desconhecido";
  strings.unlicensedStatus = "Não licenciado";
  strings.licensedStatus = "Licenciado";
  strings.offlineGraceStatus = "Licenciado (período de tolerância offline)";
  strings.verifyDueStatus = "Verificação online necessária";
  strings.expiredStatus = "Expirada";
  strings.revokedStatus = "Revogada";
  strings.notFoundStatus = "Licença não encontrada";
  strings.productMismatchStatus = "Produto incompatível";
  strings.seatLimitStatus = "Limite de ativações atingido";
  strings.invalidCacheStatus = "A licença salva é inválida";
  strings.networkUnavailableStatus = "Servidor de licenças indisponível";
  strings.errorStatus = "Erro de licença";
  strings.seatsLabel = "Assentos";
  strings.maxActivationsLabel = "Máximo de ativações";
  strings.expiresAtLabel = "Expira em";
  strings.noExpirationText = "Sem expiração";
  strings.offlineGraceDetail =
    "Usando a licença salva durante o período de tolerância para nova tentativa offline.";
  strings.errorSeatLimit = "Esta licença atingiu seu limite de ativações.";
  strings.errorExpired = "Esta licença expirou.";
  strings.errorRevoked = "Esta licença foi revogada.";
  strings.errorNotFound = "A chave de licença não foi encontrada.";
  strings.errorProductMismatch = "Esta licença é para um produto diferente.";
  strings.errorNetwork =
    "Não foi possível acessar o servidor de licenças. Verifique sua conexão e tente novamente.";
  strings.errorRequestRejected = "O servidor de licenças rejeitou a solicitação.";
  strings.errorInvalidResponse = "O servidor de licenças retornou uma resposta inválida.";
  strings.errorInvalidSignature = "A assinatura da licença salva é inválida.";
  strings.errorInvalidCache = "O arquivo de licença salvo é inválido.";
  strings.errorCacheIo = "Não foi possível ler ou gravar o arquivo de licença salvo.";
  strings.errorCacheMissing = "Nenhuma licença salva foi encontrada.";
  strings.errorMachineMismatch = "Esta licença foi ativada em outra máquina.";
  strings.errorConfig = "A configuração do License SDK está incompleta.";
  strings.errorFallback = "A operação de licença falhou.";
  return strings;
}

LicenseActivationStrings simplifiedChineseLicenseActivationStrings() {
  LicenseActivationStrings strings;
  strings.licenseKeyLabel = "许可证密钥";
  strings.licenseKeyPlaceholder = "输入许可证密钥";
  strings.activateButton = "激活";
  strings.deactivateButton = "停用";
  strings.trialButton = "免费试用";
  strings.checkingStatus = "正在检查许可证...";
  strings.activatingStatus = "正在激活...";
  strings.deactivatingStatus = "正在停用...";
  strings.unknownStatus = "许可证状态未知";
  strings.unlicensedStatus = "未授权";
  strings.licensedStatus = "已授权";
  strings.offlineGraceStatus = "已授权（离线宽限期）";
  strings.verifyDueStatus = "需要在线验证";
  strings.expiredStatus = "已过期";
  strings.revokedStatus = "已撤销";
  strings.notFoundStatus = "未找到许可证";
  strings.productMismatchStatus = "产品不匹配";
  strings.seatLimitStatus = "已达到激活限制";
  strings.invalidCacheStatus = "保存的许可证无效";
  strings.networkUnavailableStatus = "许可证服务器不可用";
  strings.errorStatus = "许可证错误";
  strings.seatsLabel = "席位";
  strings.maxActivationsLabel = "最大激活数";
  strings.expiresAtLabel = "到期时间";
  strings.noExpirationText = "无到期时间";
  strings.offlineGraceDetail = "正在离线重试宽限期内使用保存的许可证。";
  strings.errorSeatLimit = "此许可证已达到激活限制。";
  strings.errorExpired = "此许可证已过期。";
  strings.errorRevoked = "此许可证已被撤销。";
  strings.errorNotFound = "未找到许可证密钥。";
  strings.errorProductMismatch = "此许可证适用于其他产品。";
  strings.errorNetwork = "无法连接到许可证服务器。请检查连接后重试。";
  strings.errorRequestRejected = "许可证服务器拒绝了请求。";
  strings.errorInvalidResponse = "许可证服务器返回了无效响应。";
  strings.errorInvalidSignature = "保存的许可证签名无效。";
  strings.errorInvalidCache = "保存的许可证文件无效。";
  strings.errorCacheIo = "无法读取或写入保存的许可证文件。";
  strings.errorCacheMissing = "未找到保存的许可证。";
  strings.errorMachineMismatch = "此许可证已在另一台机器上激活。";
  strings.errorConfig = "License SDK 配置不完整。";
  strings.errorFallback = "许可证操作失败。";
  return strings;
}

LicenseActivationStrings frenchLicenseActivationStrings() {
  LicenseActivationStrings strings;
  strings.licenseKeyLabel = "Clé de licence";
  strings.licenseKeyPlaceholder = "Saisir la clé de licence";
  strings.activateButton = "Activer";
  strings.deactivateButton = "Désactiver";
  strings.trialButton = "Essai gratuit";
  strings.checkingStatus = "Vérification de la licence...";
  strings.activatingStatus = "Activation...";
  strings.deactivatingStatus = "Désactivation...";
  strings.unknownStatus = "État de la licence inconnu";
  strings.unlicensedStatus = "Non licencié";
  strings.licensedStatus = "Licencié";
  strings.offlineGraceStatus = "Licencié (période de grâce hors ligne)";
  strings.verifyDueStatus = "Vérification en ligne requise";
  strings.expiredStatus = "Expirée";
  strings.revokedStatus = "Révoquée";
  strings.notFoundStatus = "Licence introuvable";
  strings.productMismatchStatus = "Produit non correspondant";
  strings.seatLimitStatus = "Limite d'activations atteinte";
  strings.invalidCacheStatus = "La licence enregistrée est invalide";
  strings.networkUnavailableStatus = "Serveur de licences indisponible";
  strings.errorStatus = "Erreur de licence";
  strings.seatsLabel = "Postes";
  strings.maxActivationsLabel = "Activations maximales";
  strings.expiresAtLabel = "Expire le";
  strings.noExpirationText = "Aucune expiration";
  strings.offlineGraceDetail =
    "Utilisation de la licence enregistrée pendant la période de grâce de nouvelle tentative hors ligne.";
  strings.errorSeatLimit = "Cette licence a atteint sa limite d'activations.";
  strings.errorExpired = "Cette licence a expiré.";
  strings.errorRevoked = "Cette licence a été révoquée.";
  strings.errorNotFound = "La clé de licence est introuvable.";
  strings.errorProductMismatch = "Cette licence est destinée à un autre produit.";
  strings.errorNetwork =
    "Impossible de joindre le serveur de licences. Vérifiez votre connexion et réessayez.";
  strings.errorRequestRejected = "Le serveur de licences a rejeté la requête.";
  strings.errorInvalidResponse = "Le serveur de licences a renvoyé une réponse invalide.";
  strings.errorInvalidSignature = "La signature de la licence enregistrée est invalide.";
  strings.errorInvalidCache = "Le fichier de licence enregistré est invalide.";
  strings.errorCacheIo = "Impossible de lire ou d'écrire le fichier de licence enregistré.";
  strings.errorCacheMissing = "Aucune licence enregistrée n'a été trouvée.";
  strings.errorMachineMismatch = "Cette licence a été activée sur une autre machine.";
  strings.errorConfig = "La configuration du License SDK est incomplète.";
  strings.errorFallback = "L'opération de licence a échoué.";
  return strings;
}

LicenseActivationStrings russianLicenseActivationStrings() {
  LicenseActivationStrings strings;
  strings.licenseKeyLabel = "Лицензионный ключ";
  strings.licenseKeyPlaceholder = "Введите лицензионный ключ";
  strings.activateButton = "Активировать";
  strings.deactivateButton = "Деактивировать";
  strings.trialButton = "Начать бесплатный пробный период";
  strings.checkingStatus = "Проверка лицензии...";
  strings.activatingStatus = "Активация...";
  strings.deactivatingStatus = "Деактивация...";
  strings.unknownStatus = "Статус лицензии неизвестен";
  strings.unlicensedStatus = "Нет лицензии";
  strings.licensedStatus = "Лицензировано";
  strings.offlineGraceStatus = "Лицензировано (офлайн-льготный период)";
  strings.verifyDueStatus = "Требуется онлайн-проверка";
  strings.expiredStatus = "Истекла";
  strings.revokedStatus = "Отозвана";
  strings.notFoundStatus = "Лицензия не найдена";
  strings.productMismatchStatus = "Несоответствие продукта";
  strings.seatLimitStatus = "Достигнут лимит активаций";
  strings.invalidCacheStatus = "Сохраненная лицензия недействительна";
  strings.networkUnavailableStatus = "Сервер лицензий недоступен";
  strings.errorStatus = "Ошибка лицензии";
  strings.seatsLabel = "Места";
  strings.maxActivationsLabel = "Максимум активаций";
  strings.expiresAtLabel = "Истекает";
  strings.noExpirationText = "Без срока действия";
  strings.offlineGraceDetail =
    "Используется сохраненная лицензия в течение льготного периода повторной попытки офлайн.";
  strings.errorSeatLimit = "Эта лицензия достигла лимита активаций.";
  strings.errorExpired = "Срок действия этой лицензии истек.";
  strings.errorRevoked = "Эта лицензия была отозвана.";
  strings.errorNotFound = "Лицензионный ключ не найден.";
  strings.errorProductMismatch = "Эта лицензия предназначена для другого продукта.";
  strings.errorNetwork =
    "Не удалось подключиться к серверу лицензий. Проверьте подключение и повторите попытку.";
  strings.errorRequestRejected = "Сервер лицензий отклонил запрос.";
  strings.errorInvalidResponse = "Сервер лицензий вернул недействительный ответ.";
  strings.errorInvalidSignature = "Подпись сохраненной лицензии недействительна.";
  strings.errorInvalidCache = "Сохраненный файл лицензии недействителен.";
  strings.errorCacheIo = "Не удалось прочитать или записать сохраненный файл лицензии.";
  strings.errorCacheMissing = "Сохраненная лицензия не найдена.";
  strings.errorMachineMismatch = "Эта лицензия была активирована на другом компьютере.";
  strings.errorConfig = "Конфигурация License SDK неполная.";
  strings.errorFallback = "Операция с лицензией не выполнена.";
  return strings;
}

LicenseActivationStrings koreanLicenseActivationStrings() {
  LicenseActivationStrings strings;
  strings.licenseKeyLabel = "라이선스 키";
  strings.licenseKeyPlaceholder = "라이선스 키 입력";
  strings.activateButton = "활성화";
  strings.deactivateButton = "비활성화";
  strings.trialButton = "무료 체험 시작";
  strings.checkingStatus = "라이선스 확인 중...";
  strings.activatingStatus = "활성화 중...";
  strings.deactivatingStatus = "비활성화 중...";
  strings.unknownStatus = "라이선스 상태 알 수 없음";
  strings.unlicensedStatus = "라이선스 없음";
  strings.licensedStatus = "라이선스 있음";
  strings.offlineGraceStatus = "라이선스 있음(오프라인 유예 기간)";
  strings.verifyDueStatus = "온라인 확인 필요";
  strings.expiredStatus = "만료됨";
  strings.revokedStatus = "취소됨";
  strings.notFoundStatus = "라이선스를 찾을 수 없음";
  strings.productMismatchStatus = "제품 불일치";
  strings.seatLimitStatus = "활성화 한도 도달";
  strings.invalidCacheStatus = "저장된 라이선스가 유효하지 않음";
  strings.networkUnavailableStatus = "라이선스 서버를 사용할 수 없음";
  strings.errorStatus = "라이선스 오류";
  strings.seatsLabel = "사용 좌석";
  strings.maxActivationsLabel = "최대 활성화 수";
  strings.expiresAtLabel = "만료일";
  strings.noExpirationText = "만료 없음";
  strings.offlineGraceDetail =
    "오프라인 재시도 유예 기간 동안 저장된 라이선스를 사용 중입니다.";
  strings.errorSeatLimit = "이 라이선스는 활성화 한도에 도달했습니다.";
  strings.errorExpired = "이 라이선스는 만료되었습니다.";
  strings.errorRevoked = "이 라이선스는 취소되었습니다.";
  strings.errorNotFound = "라이선스 키를 찾을 수 없습니다.";
  strings.errorProductMismatch = "이 라이선스는 다른 제품용입니다.";
  strings.errorNetwork =
    "라이선스 서버에 연결할 수 없습니다. 연결을 확인하고 다시 시도하세요.";
  strings.errorRequestRejected = "라이선스 서버가 요청을 거부했습니다.";
  strings.errorInvalidResponse = "라이선스 서버가 유효하지 않은 응답을 반환했습니다.";
  strings.errorInvalidSignature = "저장된 라이선스 서명이 유효하지 않습니다.";
  strings.errorInvalidCache = "저장된 라이선스 파일이 유효하지 않습니다.";
  strings.errorCacheIo = "저장된 라이선스 파일을 읽거나 쓸 수 없습니다.";
  strings.errorCacheMissing = "저장된 라이선스를 찾을 수 없습니다.";
  strings.errorMachineMismatch = "이 라이선스는 다른 컴퓨터에서 활성화되었습니다.";
  strings.errorConfig = "License SDK 설정이 완료되지 않았습니다.";
  strings.errorFallback = "라이선스 작업에 실패했습니다.";
  return strings;
}

std::string formatLicenseActivationDate(std::chrono::system_clock::time_point value) {
  const std::time_t seconds = std::chrono::system_clock::to_time_t(value);
  std::tm time{};

#ifdef _WIN32
  gmtime_s(&time, &seconds);
#else
  gmtime_r(&seconds, &time);
#endif

  std::ostringstream output;
  output << std::put_time(&time, "%Y-%m-%d %H:%M UTC");
  return output.str();
}

std::string licenseActivationErrorMessage(
  ErrorCode code,
  const LicenseActivationStrings& strings,
  const std::string& fallbackMessage
) {
  switch (code) {
    case ErrorCode::None:
      return {};
    case ErrorCode::SeatLimit:
      return strings.errorSeatLimit;
    case ErrorCode::Expired:
      return strings.errorExpired;
    case ErrorCode::Revoked:
      return strings.errorRevoked;
    case ErrorCode::NotFound:
      return strings.errorNotFound;
    case ErrorCode::ProductMismatch:
      return strings.errorProductMismatch;
    case ErrorCode::TrialNotAvailable:
    case ErrorCode::TrialExpired:
      break;
    case ErrorCode::NetworkError:
      return strings.errorNetwork;
    case ErrorCode::RequestRejected:
      return strings.errorRequestRejected;
    case ErrorCode::InvalidResponse:
      return strings.errorInvalidResponse;
    case ErrorCode::InvalidSignature:
      return strings.errorInvalidSignature;
    case ErrorCode::InvalidCache:
      return strings.errorInvalidCache;
    case ErrorCode::CacheIoError:
      return strings.errorCacheIo;
    case ErrorCode::CacheMissing:
      return strings.errorCacheMissing;
    case ErrorCode::MachineMismatch:
      return strings.errorMachineMismatch;
    case ErrorCode::ConfigError:
      return strings.errorConfig;
  }

  if (!fallbackMessage.empty()) {
    return fallbackMessage;
  }

  return strings.errorFallback;
}

LicenseActivationViewModel buildLicenseActivationView(
  const LicenseActivationViewInput& input,
  const LicenseActivationStrings& strings
) {
  LicenseActivationViewModel view;
  view.statusText = input.busy && !input.busyMessage.empty()
    ? input.busyMessage
    : statusTextForState(input.state, strings);

  const auto maxActivations = resolvedMaxActivations(input);
  const auto expiresAt = resolvedExpiresAt(input);
  std::vector<std::string> details;

  if (input.seatsUsed && maxActivations) {
    details.push_back(strings.seatsLabel + ": " +
                      std::to_string(*input.seatsUsed) + "/" +
                      std::to_string(*maxActivations));
  } else if (maxActivations) {
    details.push_back(strings.maxActivationsLabel + ": " + std::to_string(*maxActivations));
  }

  if (expiresAt) {
    details.push_back(strings.expiresAtLabel + ": " + formatLicenseActivationDate(*expiresAt));
  } else if (shouldShowNoExpiration(input)) {
    details.push_back(strings.expiresAtLabel + ": " + strings.noExpirationText);
  }

  if (input.state == LicenseState::LicensedOfflineGrace) {
    details.push_back(strings.offlineGraceDetail);
  }

  view.detailsText = joinLines(details);

  if (input.error != ErrorCode::None &&
      !(input.state == LicenseState::Unlicensed && input.error == ErrorCode::CacheMissing)) {
    view.errorText = licenseActivationErrorMessage(input.error, strings, input.errorMessage);
  }

  view.canActivate = !input.busy && input.hasLicenseKey && !isLicensedLike(input.state);
  view.canDeactivate = !input.busy && input.license.has_value();
  return view;
}

} // namespace otomarket::license
