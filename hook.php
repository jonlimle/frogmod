<?

define('DB_USER', '');
define('DB_PASSWORD', '');
define('DB_NAME', '');
define('DB_HOST', 'localhost');

define('PLAYERS_TABLE', 'sauer_players');

require_once 'ez_sql_core.php';
require_once 'ez_sql_mysql.php';

$db = new ezSQL_mysql(DB_USER,DB_PASSWORD,DB_NAME,DB_HOST);

$name = $db->escape($_GET['name']);
$ip = $db->escape($_GET['ip']);

$row = $db->get_row("SELECT * FROM `".PLAYERS_TABLE."` WHERE `name` = '$name' AND `ip` = '$ip'");

if($row) $db->query("UPDATE `".PLAYERS_TABLE."` SET `last_connect` = NOW(), `connects` = '".($row->connects+1)."'  WHERE `id` = '$row->id'");
else $db->query("INSERT INTO `".PLAYERS_TABLE."` (`name`, `ip`, `first_connect`, `last_connect`, `connects`) VALUES('$name', '$ip', NOW(), NOW(), 1)");

echo "Done.";
