import { Injectable, Pipe, PipeTransform, inject, signal, computed } from '@angular/core';
import { UserPreferences } from '../models/jira.models';

export interface LanguageOption {
  code: string;
  name: string;
  nativeName: string;
  flag: string;
}

export const SUPPORTED_LANGUAGES: LanguageOption[] = [
  { code: 'en', name: 'English', nativeName: 'English', flag: '🇺🇸' },
  { code: 'de', name: 'German', nativeName: 'Deutsch', flag: '🇩🇪' },
  { code: 'fr', name: 'French', nativeName: 'Français', flag: '🇫🇷' },
  { code: 'es', name: 'Spanish', nativeName: 'Español', flag: '🇪🇸' },
  { code: 'ja', name: 'Japanese', nativeName: '日本語', flag: '🇯🇵' },
  { code: 'zh', name: 'Chinese', nativeName: '中文 (简体)', flag: '🇨🇳' }
];

const TRANSLATIONS: Record<string, Record<string, string>> = {
  en: {
    'nav.brand': 'JiraLike',
    'nav.dashboards': 'Dashboards',
    'nav.boards': 'Agile Boards',
    'nav.backlog': 'Agile Backlog',
    'nav.search': 'Search & JQL',
    'nav.reports': 'Reports',
    'nav.roadmaps': 'Advanced Roadmap',
    'nav.gantt': 'Gantt',
    'nav.states': 'States',
    'nav.workflows': 'Workflows',
    'nav.releases': 'Releases & Components',
    'nav.customFields': 'Custom Fields & Screens',
    'nav.permissions': 'Permissions & Security',
    'nav.automation': 'Automation',
    'nav.webhooks': 'Webhooks & Notifications',
    'nav.mailHandlers': 'Mail Handlers',
    'nav.serviceDesk': 'Service Desk',
    'nav.customerPortal': 'Customer Portal',
    'nav.trashBin': 'Trash Bin',
    'nav.sprintEditor': 'Sprint Editor',
    'nav.developerDocs': 'Developer Docs',
    'nav.create': '+ Create (c)',
    'nav.live': 'Live',
    'nav.syncing': 'Syncing',
    'nav.logout': 'Logout',
    'nav.preferences': 'Preferences & Locale',
    'nav.security': 'Security, 2FA & SSO',

    'common.save': 'Save',
    'common.cancel': 'Cancel',
    'common.delete': 'Delete',
    'common.edit': 'Edit',
    'common.create': 'Create',
    'common.search': 'Search',
    'common.filter': 'Filter',
    'common.close': 'Close',
    'common.loading': 'Loading...',
    'common.status': 'Status',
    'common.priority': 'Priority',
    'common.assignee': 'Assignee',
    'common.reporter': 'Reporter',
    'common.actions': 'Actions',
    'common.confirm': 'Confirm',
    'common.yes': 'Yes',
    'common.no': 'No',
    'common.success': 'Success',
    'common.error': 'Error',
    'common.name': 'Name',
    'common.description': 'Description',
    'common.key': 'Key',
    'common.summary': 'Summary',
    'common.type': 'Type',
    'common.details': 'Details',
    'common.preview': 'Preview',
    'common.active': 'Active',
    'common.disabled': 'Disabled',

    'board.toDo': 'To Do',
    'board.inProgress': 'In Progress',
    'board.done': 'Done',
    'board.quickFilters': 'Quick Filters',
    'board.onlyMyIssues': 'Only My Issues',
    'board.recentlyUpdated': 'Recently Updated',
    'board.completeSprint': 'Complete Sprint',
    'board.startSprint': 'Start Sprint',
    'board.daysRemaining': 'days remaining',
    'board.createIssue': 'Create Issue',

    'preferences.title': 'User Preferences & Localization',
    'preferences.subtitle': 'Configure display language, regional timezone, and date/time formatting standards',
    'preferences.language': 'Display Language',
    'preferences.timezone': 'Regional Timezone',
    'preferences.dateFormat': 'Date Format',
    'preferences.timeFormat': 'Time Format',
    'preferences.saved': 'Preferences saved successfully!',
    'preferences.saveBtn': 'Save Preferences',
    'preferences.previewTitle': 'Formatted Date & Time Preview',
    'preferences.previewDesc': 'Your configured regional format rendered in real-time:',
    'preferences.switchLanguage': 'Switch Language',

    'docs.title': 'Developer Portal & OpenAPI 3.0 Documentation',
    'docs.subtitle': 'Interactive REST API specification, token-bucket rate limiting telemetry, and Swagger UI Explorer',
    'docs.rawSpec': 'Raw OpenAPI JSON Spec',
    'docs.download': 'Download OpenAPI 3.0.0 JSON',
    'docs.rateLimitsTitle': 'Token-Bucket Rate Limiter Governance',
    'docs.rateLimitsDesc': 'Real-time FastCGI rate-limiting status and burst simulator',
    'docs.clientType': 'Client Type',
    'docs.clientKey': 'Client Key',
    'docs.tokensRemaining': 'Remaining Tokens',
    'docs.burstCapacity': 'Burst Capacity',
    'docs.refillRate': 'Refill Rate',
    'docs.resetIn': 'Reset Time',
    'docs.simulateAbuse': 'Test Burst & Simulate 429',
    'docs.resetBuckets': 'Reset Rate Limit Bucket',
    'docs.testSuccess': 'Requests processed successfully',
    'docs.test429': 'Rate limit hit! Received HTTP 429 Too Many Requests with Retry-After header',
    'docs.apiExplorer': 'Interactive Endpoint Explorer',
    'docs.tryItOut': 'Try it out',
    'docs.execute': 'Execute'
  },

  de: {
    'nav.brand': 'JiraLike',
    'nav.dashboards': 'Übersichten',
    'nav.boards': 'Agile Boards',
    'nav.backlog': 'Agiles Backlog',
    'nav.search': 'Suche & JQL',
    'nav.reports': 'Berichte',
    'nav.roadmaps': 'Erweiterte Roadmap',
    'nav.gantt': 'Gantt-Diagramm',
    'nav.states': 'Statuswerte',
    'nav.workflows': 'Arbeitsabläufe',
    'nav.releases': 'Releases & Komponenten',
    'nav.customFields': 'Benutzerdefinierte Felder & Masken',
    'nav.permissions': 'Berechtigungen & Sicherheit',
    'nav.automation': 'Automatisierung',
    'nav.webhooks': 'Webhooks & Benachrichtigungen',
    'nav.mailHandlers': 'E-Mail-Verarbeitung',
    'nav.serviceDesk': 'Service Desk',
    'nav.customerPortal': 'Kundenportal',
    'nav.trashBin': 'Papierkorb',
    'nav.sprintEditor': 'Sprint-Editor',
    'nav.developerDocs': 'Entwickler-Dokumentation',
    'nav.create': '+ Erstellen (c)',
    'nav.live': 'Live',
    'nav.syncing': 'Synchronisiert',
    'nav.logout': 'Abmelden',
    'nav.preferences': 'Einstellungen & Sprache',
    'nav.security': 'Sicherheit, 2FA & SSO',

    'common.save': 'Speichern',
    'common.cancel': 'Abbrechen',
    'common.delete': 'Löschen',
    'common.edit': 'Bearbeiten',
    'common.create': 'Erstellen',
    'common.search': 'Suchen',
    'common.filter': 'Filtern',
    'common.close': 'Schließen',
    'common.loading': 'Wird geladen...',
    'common.status': 'Status',
    'common.priority': 'Priorität',
    'common.assignee': 'Zugewiesen an',
    'common.reporter': 'Gemeldet von',
    'common.actions': 'Aktionen',
    'common.confirm': 'Bestätigen',
    'common.yes': 'Ja',
    'common.no': 'Nein',
    'common.success': 'Erfolg',
    'common.error': 'Fehler',
    'common.name': 'Name',
    'common.description': 'Beschreibung',
    'common.key': 'Schlüssel',
    'common.summary': 'Zusammenfassung',
    'common.type': 'Typ',
    'common.details': 'Details',
    'common.preview': 'Vorschau',
    'common.active': 'Aktiv',
    'common.disabled': 'Deaktiviert',

    'board.toDo': 'Zu erledigen',
    'board.inProgress': 'In Bearbeitung',
    'board.done': 'Fertig',
    'board.quickFilters': 'Schnellfilter',
    'board.onlyMyIssues': 'Nur meine Vorgänge',
    'board.recentlyUpdated': 'Kürzlich aktualisiert',
    'board.completeSprint': 'Sprint abschließen',
    'board.startSprint': 'Sprint starten',
    'board.daysRemaining': 'Tage verbleibend',
    'board.createIssue': 'Vorgang erstellen',

    'preferences.title': 'Benutzereinstellungen & Lokalisierung',
    'preferences.subtitle': 'Konfigurieren Sie Anzeigesprache, Zeitzone sowie Datums- und Uhrzeitformate',
    'preferences.language': 'Anzeigesprache',
    'preferences.timezone': 'Regionale Zeitzone',
    'preferences.dateFormat': 'Datumsformat',
    'preferences.timeFormat': 'Zeitformat',
    'preferences.saved': 'Einstellungen erfolgreich gespeichert!',
    'preferences.saveBtn': 'Einstellungen speichern',
    'preferences.previewTitle': 'Vorschau auf Datums- & Uhrzeitformat',
    'preferences.previewDesc': 'Ihr konfiguriertes Format in Echtzeit:',
    'preferences.switchLanguage': 'Sprache wechseln',

    'docs.title': 'Entwicklerportal & OpenAPI 3.0 Dokumentation',
    'docs.subtitle': 'Interaktive REST-API-Spezifikation, Token-Bucket Ratenbegrenzung und Swagger UI Explorer',
    'docs.rawSpec': 'OpenAPI JSON Spezifikation',
    'docs.download': 'OpenAPI 3.0.0 JSON herunterladen',
    'docs.rateLimitsTitle': 'Token-Bucket Ratenbegrenzung',
    'docs.rateLimitsDesc': 'Echtzeitstatus der FastCGI-Ratenbegrenzung und Lastsimulator',
    'docs.clientType': 'Client-Typ',
    'docs.clientKey': 'Client-Schlüssel',
    'docs.tokensRemaining': 'Verbleibende Tokens',
    'docs.burstCapacity': 'Burst-Kapazität',
    'docs.refillRate': 'Auffüllrate',
    'docs.resetIn': 'Zurücksetzen um',
    'docs.simulateAbuse': 'Burst testen & HTTP 429 simulieren',
    'docs.resetBuckets': 'Token-Bucket zurücksetzen',
    'docs.testSuccess': 'Anfragen erfolgreich verarbeitet',
    'docs.test429': 'Ratenbegrenzung erreicht! HTTP 429 Too Many Requests empfangen',
    'docs.apiExplorer': 'Interaktiver Endpunkt-Explorer',
    'docs.tryItOut': 'Ausprobieren',
    'docs.execute': 'Ausführen'
  },

  fr: {
    'nav.brand': 'JiraLike',
    'nav.dashboards': 'Tableaux de bord',
    'nav.boards': 'Tableaux agiles',
    'nav.backlog': 'Backlog agile',
    'nav.search': 'Recherche & JQL',
    'nav.reports': 'Rapports',
    'nav.roadmaps': 'Feuille de route avancée',
    'nav.gantt': 'Diagramme de Gantt',
    'nav.states': 'États',
    'nav.workflows': 'Flux de travail',
    'nav.releases': 'Versions & Composants',
    'nav.customFields': 'Champs personnalisés & Écrans',
    'nav.permissions': 'Permissions & Sécurité',
    'nav.automation': 'Automatisation',
    'nav.webhooks': 'Webhooks & Notifications',
    'nav.mailHandlers': 'Gestion des e-mails',
    'nav.serviceDesk': 'Centre de services',
    'nav.customerPortal': 'Portail client',
    'nav.trashBin': 'Corbeille',
    'nav.sprintEditor': 'Éditeur de sprint',
    'nav.developerDocs': 'Documentation API',
    'nav.create': '+ Créer (c)',
    'nav.live': 'En direct',
    'nav.syncing': 'Synchronisation',
    'nav.logout': 'Déconnexion',
    'nav.preferences': 'Préférences & Région',
    'nav.security': 'Sécurité, 2FA & SSO',

    'common.save': 'Enregistrer',
    'common.cancel': 'Annuler',
    'common.delete': 'Supprimer',
    'common.edit': 'Modifier',
    'common.create': 'Créer',
    'common.search': 'Rechercher',
    'common.filter': 'Filtrer',
    'common.close': 'Fermer',
    'common.loading': 'Chargement...',
    'common.status': 'Statut',
    'common.priority': 'Priorité',
    'common.assignee': 'Assigné à',
    'common.reporter': 'Rapporteur',
    'common.actions': 'Actions',
    'common.confirm': 'Confirmer',
    'common.yes': 'Oui',
    'common.no': 'Non',
    'common.success': 'Succès',
    'common.error': 'Erreur',
    'common.name': 'Nom',
    'common.description': 'Description',
    'common.key': 'Clé',
    'common.summary': 'Résumé',
    'common.type': 'Type',
    'common.details': 'Détails',
    'common.preview': 'Aperçu',
    'common.active': 'Actif',
    'common.disabled': 'Désactivé',

    'board.toDo': 'À faire',
    'board.inProgress': 'En cours',
    'board.done': 'Terminé',
    'board.quickFilters': 'Filtres rapides',
    'board.onlyMyIssues': 'Mes tickets uniquement',
    'board.recentlyUpdated': 'Mis à jour récemment',
    'board.completeSprint': 'Terminer le sprint',
    'board.startSprint': 'Démarrer le sprint',
    'board.daysRemaining': 'jours restants',
    'board.createIssue': 'Créer un ticket',

    'preferences.title': 'Préférences utilisateur & Localisation',
    'preferences.subtitle': 'Configurez la langue d’affichage, le fuseau horaire et les formats de date et heure',
    'preferences.language': 'Langue d’affichage',
    'preferences.timezone': 'Fuseau horaire',
    'preferences.dateFormat': 'Format de date',
    'preferences.timeFormat': 'Format d’heure',
    'preferences.saved': 'Préférences enregistrées avec succès !',
    'preferences.saveBtn': 'Enregistrer les préférences',
    'preferences.previewTitle': 'Aperçu de la date et de l’heure',
    'preferences.previewDesc': 'Votre format régional rendu en temps réel :',
    'preferences.switchLanguage': 'Changer de langue',

    'docs.title': 'Portail développeur & Spécification OpenAPI 3.0',
    'docs.subtitle': 'Spécification interactive de l’API REST, gouvernance par limitation de débit et Swagger UI',
    'docs.rawSpec': 'Spécification OpenAPI brute',
    'docs.download': 'Télécharger le schéma OpenAPI 3.0.0 JSON',
    'docs.rateLimitsTitle': 'Gouvernance de la limitation de débit (Token-Bucket)',
    'docs.rateLimitsDesc': 'Télémétrie en temps réel du serveur FastCGI et simulateur de charge',
    'docs.clientType': 'Type de client',
    'docs.clientKey': 'Identifiant client',
    'docs.tokensRemaining': 'Jetons restants',
    'docs.burstCapacity': 'Capacité maximale',
    'docs.refillRate': 'Vitesse de rechargement',
    'docs.resetIn': 'Réinitialisation à',
    'docs.simulateAbuse': 'Tester le débit & Simuler HTTP 429',
    'docs.resetBuckets': 'Réinitialiser les jetons',
    'docs.testSuccess': 'Requêtes traitées avec succès',
    'docs.test429': 'Limite atteinte ! Code HTTP 429 Too Many Requests reçu',
    'docs.apiExplorer': 'Explorateur d’API interactif',
    'docs.tryItOut': 'Tester',
    'docs.execute': 'Exécuter'
  },

  es: {
    'nav.brand': 'JiraLike',
    'nav.dashboards': 'Paneles',
    'nav.boards': 'Tableros ágiles',
    'nav.backlog': 'Backlog ágil',
    'nav.search': 'Búsqueda & JQL',
    'nav.reports': 'Informes',
    'nav.roadmaps': 'Hoja de ruta avanzada',
    'nav.gantt': 'Diagrama de Gantt',
    'nav.states': 'Estados',
    'nav.workflows': 'Flujos de trabajo',
    'nav.releases': 'Versiones & Componentes',
    'nav.customFields': 'Campos personalizados & Pantallas',
    'nav.permissions': 'Permisos & Seguridad',
    'nav.automation': 'Automatización',
    'nav.webhooks': 'Webhooks & Notificaciones',
    'nav.mailHandlers': 'Recepción de correo',
    'nav.serviceDesk': 'Mesa de ayuda',
    'nav.customerPortal': 'Portal del cliente',
    'nav.trashBin': 'Papelera',
    'nav.sprintEditor': 'Editor de sprints',
    'nav.developerDocs': 'Documentación API',
    'nav.create': '+ Crear (c)',
    'nav.live': 'En vivo',
    'nav.syncing': 'Sincronizando',
    'nav.logout': 'Cerrar sesión',
    'nav.preferences': 'Preferencias e idioma',
    'nav.security': 'Seguridad, 2FA & SSO',

    'common.save': 'Guardar',
    'common.cancel': 'Cancelar',
    'common.delete': 'Eliminar',
    'common.edit': 'Editar',
    'common.create': 'Crear',
    'common.search': 'Buscar',
    'common.filter': 'Filtrar',
    'common.close': 'Cerrar',
    'common.loading': 'Cargando...',
    'common.status': 'Estado',
    'common.priority': 'Prioridad',
    'common.assignee': 'Responsable',
    'common.reporter': 'Informador',
    'common.actions': 'Acciones',
    'common.confirm': 'Confirmar',
    'common.yes': 'Sí',
    'common.no': 'No',
    'common.success': 'Éxito',
    'common.error': 'Error',
    'common.name': 'Nombre',
    'common.description': 'Descripción',
    'common.key': 'Clave',
    'common.summary': 'Resumen',
    'common.type': 'Tipo',
    'common.details': 'Detalles',
    'common.preview': 'Vista previa',
    'common.active': 'Activo',
    'common.disabled': 'Desactivado',

    'board.toDo': 'Por hacer',
    'board.inProgress': 'En curso',
    'board.done': 'Listo',
    'board.quickFilters': 'Filtros rápidos',
    'board.onlyMyIssues': 'Solo mis incidencias',
    'board.recentlyUpdated': 'Actualizado recientemente',
    'board.completeSprint': 'Completar sprint',
    'board.startSprint': 'Iniciar sprint',
    'board.daysRemaining': 'días restantes',
    'board.createIssue': 'Crear incidencia',

    'preferences.title': 'Preferencias de usuario y localización',
    'preferences.subtitle': 'Configure idioma de la interfaz, zona horaria y formato de fecha y hora',
    'preferences.language': 'Idioma de la interfaz',
    'preferences.timezone': 'Zona horaria',
    'preferences.dateFormat': 'Formato de fecha',
    'preferences.timeFormat': 'Formato de hora',
    'preferences.saved': '¡Preferencias guardadas con éxito!',
    'preferences.saveBtn': 'Guardar preferencias',
    'preferences.previewTitle': 'Vista previa de fecha y hora',
    'preferences.previewDesc': 'Su formato configurado en tiempo real:',
    'preferences.switchLanguage': 'Cambiar idioma',

    'docs.title': 'Portal de desarrolladores y OpenAPI 3.0',
    'docs.subtitle': 'Especificación interactiva de la API REST, limitador de tasa token-bucket y Swagger UI',
    'docs.rawSpec': 'Especificación OpenAPI bruta',
    'docs.download': 'Descargar JSON OpenAPI 3.0.0',
    'docs.rateLimitsTitle': 'Gobernanza de limitación de tasa (Token-Bucket)',
    'docs.rateLimitsDesc': 'Telemetría de tasa en FastCGI y simulador de ráfagas',
    'docs.clientType': 'Tipo de cliente',
    'docs.clientKey': 'Clave de cliente',
    'docs.tokensRemaining': 'Tokens restantes',
    'docs.burstCapacity': 'Capacidad de ráfaga',
    'docs.refillRate': 'Tasa de recarga',
    'docs.resetIn': 'Reinicio a las',
    'docs.simulateAbuse': 'Probar ráfaga y simular HTTP 429',
    'docs.resetBuckets': 'Reiniciar cubeta de tokens',
    'docs.testSuccess': 'Solicitudes procesadas correctamente',
    'docs.test429': '¡Límite alcanzado! Respuesta HTTP 429 con cabecera Retry-After',
    'docs.apiExplorer': 'Explorador interactivo de endpoints',
    'docs.tryItOut': 'Probar',
    'docs.execute': 'Ejecutar'
  },

  ja: {
    'nav.brand': 'JiraLike',
    'nav.dashboards': 'ダッシュボード',
    'nav.boards': 'アジャイルボード',
    'nav.backlog': 'バックログ',
    'nav.search': '検索 & JQL',
    'nav.reports': 'レポート',
    'nav.roadmaps': '高度なロードマップ',
    'nav.gantt': 'ガントチャート',
    'nav.states': 'ステータス',
    'nav.workflows': 'ワークフロー',
    'nav.releases': 'リリース & コンポーネント',
    'nav.customFields': 'カスタムフィールド & 画面',
    'nav.permissions': '権限 & セキュリティ',
    'nav.automation': 'オートメーション',
    'nav.webhooks': 'Webhook & 通知',
    'nav.mailHandlers': 'メール受信ハンドラー',
    'nav.serviceDesk': 'サービスデスク',
    'nav.customerPortal': 'カスタマーポータル',
    'nav.trashBin': 'ゴミ箱',
    'nav.sprintEditor': 'スプリント設定',
    'nav.developerDocs': '開発者向けドキュメント',
    'nav.create': '+ 作成 (c)',
    'nav.live': 'リアルタイム',
    'nav.syncing': '同期中',
    'nav.logout': 'ログアウト',
    'nav.preferences': 'ユーザー設定 & 言語',
    'nav.security': 'セキュリティ、2FA & SSO',

    'common.save': '保存',
    'common.cancel': 'キャンセル',
    'common.delete': '削除',
    'common.edit': '編集',
    'common.create': '作成',
    'common.search': '検索',
    'common.filter': '絞り込み',
    'common.close': '閉じる',
    'common.loading': '読み込み中...',
    'common.status': 'ステータス',
    'common.priority': '優先度',
    'common.assignee': '担当者',
    'common.reporter': '報告者',
    'common.actions': '操作',
    'common.confirm': '確認',
    'common.yes': 'はい',
    'common.no': 'いいえ',
    'common.success': '成功',
    'common.error': 'エラー',
    'common.name': '名前',
    'common.description': '説明',
    'common.key': 'キー',
    'common.summary': '要約',
    'common.type': 'タイプ',
    'common.details': '詳細',
    'common.preview': 'プレビュー',
    'common.active': '有効',
    'common.disabled': '無効',

    'board.toDo': '未着手',
    'board.inProgress': '進行中',
    'board.done': '完了',
    'board.quickFilters': 'クイックフィルター',
    'board.onlyMyIssues': '自分が担当の課題のみ',
    'board.recentlyUpdated': '最近更新された課題',
    'board.completeSprint': 'スプリントを完了',
    'board.startSprint': 'スプリントを開始',
    'board.daysRemaining': '日残り',
    'board.createIssue': '課題を作成',

    'preferences.title': 'ユーザー環境設定 & 地域設定',
    'preferences.subtitle': '表示言語、タイムゾーン、および日付・時刻の書式基準を設定します',
    'preferences.language': '表示言語',
    'preferences.timezone': 'タイムゾーン',
    'preferences.dateFormat': '日付フォーマット',
    'preferences.timeFormat': '時刻フォーマット',
    'preferences.saved': '設定が正常に保存されました！',
    'preferences.saveBtn': '設定を保存',
    'preferences.previewTitle': '日付・時刻プレビュー',
    'preferences.previewDesc': '設定されたフォーマットの表示例：',
    'preferences.switchLanguage': '言語を切り替える',

    'docs.title': '開発者ポータル & OpenAPI 3.0 仕様',
    'docs.subtitle': 'インタラクティブな REST API 仕様書、Token-Bucket レートリミット監視および Swagger UI',
    'docs.rawSpec': 'OpenAPI JSON 仕様',
    'docs.download': 'OpenAPI 3.0.0 JSON をダウンロード',
    'docs.rateLimitsTitle': 'Token-Bucket レート制限ガバナンス',
    'docs.rateLimitsDesc': 'FastCGI サーバーのリアルタイム制限状態および負荷シミュレーター',
    'docs.clientType': 'クライアント種別',
    'docs.clientKey': 'クライアントキー',
    'docs.tokensRemaining': '残りトークン数',
    'docs.burstCapacity': '最大バースト容量',
    'docs.refillRate': '補充レート (秒あたり)',
    'docs.resetIn': 'リセット時刻',
    'docs.simulateAbuse': 'バーストテスト & HTTP 429 検証',
    'docs.resetBuckets': 'トークンバケットをリセット',
    'docs.testSuccess': 'リクエストは正常に処理されました',
    'docs.test429': 'レート制限発動！ Retry-After ヘッダー付き HTTP 429 を受信しました',
    'docs.apiExplorer': 'インタラクティブ API エクスプローラー',
    'docs.tryItOut': '試してみる',
    'docs.execute': '実行'
  },

  zh: {
    'nav.brand': 'JiraLike',
    'nav.dashboards': '仪表板',
    'nav.boards': '敏捷看板',
    'nav.backlog': '敏捷待办',
    'nav.search': '检索与 JQL',
    'nav.reports': '统计报表',
    'nav.roadmaps': '高级路线图',
    'nav.gantt': '甘特图',
    'nav.states': '状态列表',
    'nav.workflows': '工作流引擎',
    'nav.releases': '发布版本与模块',
    'nav.customFields': '自定义字段与界面',
    'nav.permissions': '权限与安全方案',
    'nav.automation': '无代码自动化',
    'nav.webhooks': 'Webhooks 与通知',
    'nav.mailHandlers': '入站邮件处理器',
    'nav.serviceDesk': '服务台支持',
    'nav.customerPortal': '客户门户',
    'nav.trashBin': '回收站',
    'nav.sprintEditor': '迭代设置',
    'nav.developerDocs': '开发者文档',
    'nav.create': '+ 新建事务 (c)',
    'nav.live': '实时在线',
    'nav.syncing': '同步中',
    'nav.logout': '退出登录',
    'nav.preferences': '偏好设置与本地化',
    'nav.security': '安全设置、2FA 与 SSO',

    'common.save': '保存',
    'common.cancel': '取消',
    'common.delete': '删除',
    'common.edit': '编辑',
    'common.create': '创建',
    'common.search': '搜索',
    'common.filter': '筛选',
    'common.close': '关闭',
    'common.loading': '加载中...',
    'common.status': '状态',
    'common.priority': '优先级',
    'common.assignee': '经办人',
    'common.reporter': '报告人',
    'common.actions': '操作',
    'common.confirm': '确认',
    'common.yes': '是',
    'common.no': '否',
    'common.success': '成功',
    'common.error': '错误',
    'common.name': '名称',
    'common.description': '描述',
    'common.key': '键值',
    'common.summary': '概要',
    'common.type': '类型',
    'common.details': '详情',
    'common.preview': '预览',
    'common.active': '已启用',
    'common.disabled': '已禁用',

    'board.toDo': '待办',
    'board.inProgress': '处理中',
    'board.done': '已解决',
    'board.quickFilters': '快速筛选',
    'board.onlyMyIssues': '仅我的事务',
    'board.recentlyUpdated': '最近更新',
    'board.completeSprint': '完成迭代',
    'board.startSprint': '启动迭代',
    'board.daysRemaining': '天剩余',
    'board.createIssue': '创建事务',

    'preferences.title': '个人偏好设置与地区语言',
    'preferences.subtitle': '配置系统显示语言、时区偏移以及日期与时间的格式规范',
    'preferences.language': '界面显示语言',
    'preferences.timezone': '本地时区',
    'preferences.dateFormat': '日期格式',
    'preferences.timeFormat': '时间制式',
    'preferences.saved': '偏好设置已成功保存！',
    'preferences.saveBtn': '保存配置',
    'preferences.previewTitle': '日期与时间实时预览',
    'preferences.previewDesc': '您当前配置的格式显示效果：',
    'preferences.switchLanguage': '切换界面语言',

    'docs.title': '开发者平台与 OpenAPI 3.0 文档',
    'docs.subtitle': '交互式 REST 接口规范、Token-Bucket 令牌桶限流治理与 Swagger UI 浏览器',
    'docs.rawSpec': 'OpenAPI JSON 规范',
    'docs.download': '下载 OpenAPI 3.0.0 JSON',
    'docs.rateLimitsTitle': '令牌桶限流治理 (Token-Bucket)',
    'docs.rateLimitsDesc': 'FastCGI 高性能后端实时请求速率监控与高并发压力模拟器',
    'docs.clientType': '客户端类型',
    'docs.clientKey': '客户端标识',
    'docs.tokensRemaining': '当前剩余令牌数',
    'docs.burstCapacity': '突发容量上限',
    'docs.refillRate': '令牌补充速率 (个/秒)',
    'docs.resetIn': '重置恢复时间',
    'docs.simulateAbuse': '快速请求测试 & 模拟 HTTP 429',
    'docs.resetBuckets': '清空并重置令牌桶',
    'docs.testSuccess': '请求成功处理，未超出速率限制',
    'docs.test429': '触发限流防护！收到 HTTP 429 Too Many Requests 及 Retry-After 头',
    'docs.apiExplorer': '交互式 API 浏览器',
    'docs.tryItOut': '调试接口',
    'docs.execute': '发送请求'
  }
};

@Injectable({
  providedIn: 'root'
})
export class I18nService {
  readonly currentLang = signal<string>(this.getInitialLanguage());
  readonly preferences = signal<UserPreferences>({
    language: this.getInitialLanguage(),
    timezone: 'UTC',
    date_format: 'YYYY-MM-DD',
    time_format: '24h'
  });

  private getInitialLanguage(): string {
    const saved = localStorage.getItem('jl_language');
    if (saved && TRANSLATIONS[saved]) return saved;
    const browserLang = (navigator.language || '').toLowerCase().split('-')[0];
    if (browserLang && TRANSLATIONS[browserLang]) return browserLang;
    return 'en';
  }

  setLanguage(lang: string): void {
    if (!TRANSLATIONS[lang]) return;
    this.currentLang.set(lang);
    localStorage.setItem('jl_language', lang);
    const cur = this.preferences();
    this.preferences.set({ ...cur, language: lang });
  }

  setPreferences(prefs: UserPreferences): void {
    this.preferences.set(prefs);
    if (prefs.language && TRANSLATIONS[prefs.language]) {
      this.currentLang.set(prefs.language);
      localStorage.setItem('jl_language', prefs.language);
    }
  }

  t(key: string, params?: Record<string, string | number>): string {
    const lang = this.currentLang();
    const bundle = TRANSLATIONS[lang] || TRANSLATIONS['en'];
    let str = bundle[key] || TRANSLATIONS['en']?.[key] || key;

    if (params) {
      for (const [k, v] of Object.entries(params)) {
        str = str.replace(new RegExp(`\\{${k}\\}`, 'g'), String(v));
      }
    }
    return str;
  }

  formatDate(dateInput: string | number | Date | null | undefined, includeTime = true): string {
    if (!dateInput) return '';
    const d = new Date(dateInput);
    if (isNaN(d.getTime())) return String(dateInput);

    const prefs = this.preferences();
    const df = prefs.date_format || 'YYYY-MM-DD';
    const tf = prefs.time_format || '24h';

    const year = d.getFullYear();
    const month = String(d.getMonth() + 1).padStart(2, '0');
    const day = String(d.getDate()).padStart(2, '0');

    let datePart = `${year}-${month}-${day}`;
    if (df === 'DD/MM/YYYY') {
      datePart = `${day}/${month}/${year}`;
    } else if (df === 'MM/DD/YYYY') {
      datePart = `${month}/${day}/${year}`;
    }

    if (!includeTime) {
      return datePart;
    }

    let hours = d.getHours();
    const minutes = String(d.getMinutes()).padStart(2, '0');
    let timePart = '';

    if (tf === '12h') {
      const ampm = hours >= 12 ? 'PM' : 'AM';
      hours = hours % 12;
      hours = hours ? hours : 12; // 0 becomes 12
      timePart = `${hours}:${minutes} ${ampm}`;
    } else {
      timePart = `${String(hours).padStart(2, '0')}:${minutes}`;
    }

    return `${datePart} ${timePart}`;
  }
}

@Pipe({
  name: 'i18n',
  standalone: true,
  pure: false
})
export class I18nPipe implements PipeTransform {
  private readonly i18n = inject(I18nService);

  transform(key: string, params?: Record<string, string | number>): string {
    return this.i18n.t(key, params);
  }
}

@Pipe({
  name: 'localeDate',
  standalone: true,
  pure: false
})
export class LocaleDatePipe implements PipeTransform {
  private readonly i18n = inject(I18nService);

  transform(dateInput: string | number | Date | null | undefined, includeTime = true): string {
    return this.i18n.formatDate(dateInput, includeTime);
  }
}
