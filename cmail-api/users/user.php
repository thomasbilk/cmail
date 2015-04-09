<?php

	include_once("../module.php");

	/**
	 * Class for user related things.
	 */
	class User implements Module {

		private $db;
		private $output;

		// List of end points. Format is:
		// $name => $func
		// $name name of the end point
		// $func name of the function that is called. Function must be in this class.
		private $endPoints = array(
			"get" => "get",
			"add" => "add",
			"delete" => "delete",
			"set_password" => "set_password",
			"delete_right" => "deleteRight",
			"add_right" => "addRight",
			"update_rights" => "updateRights"
		);

		/**
		 * Constructor for the user class.
		 * @param $db the db object
		 * @param $output the output object
		 */
		public function __construct($db, $output) {
			$this->db = $db;
			$this->output = $output;	
		}

		/**
		 * Returns all endpoints for this module.
		 * @return list of endpoints as keys and function name as values.
		 */
		public function getEndPoints() {
			return $this->endPoints;
		}


		/**
		 * Returns a list of users that are delegated to the authorized user. In list is the user itself.
		 * @param $write if true send user list to output module
		 * @return list of users
		 */
		private function getDelegated($write = true) {

			$sql = "SELECT user_name, (user_authdata IS NOT NULL) AS user_login 
				FROM users WHERE user_name IN (
					SELECT api_delegate AS user_name FROM api_user_delegates WHERE api_user = :api_user
				) OR user_name = :api_user";

			$auth = Auth::getInstance($this->db, $this->output);

			$params = array(
				":api_user" => $auth->getUser()
			);

			$out = $this->db->query($sql, $params, DB::F_ARRAY);

			$output = [];

			foreach($out as $user) {
				$user["modules"] = $this->getActiveModules($user);
				$output[] = $user;
			}

			if ($write) {
				$this->output->add("users", $output);
			}
			return $output;
		}

		/**
		 * Returns the given user when in database. If no user is defined, send all users.
		 * @param obj object with key username into
		 * @param $write (optional) if true give user list to the output module
		 * @output_flags users all users that matches this username (should be one)
		 * @return list of users that matches (should be one)
		 */
		public function get($obj, $write = true) {

			$auth = Auth::getInstance($this->db, $this->output);	

			if ($auth->hasRight("admin")) {
				if (!isset($obj["username"])) {
					// if no username is set, return all users
					return $this->getAll();
				} else {
					return $this->getByUser($obj, $write);
				}
			} else if ($auth->hasRight("delegate")) {
				if (isset($obj["username"]) && !empty($obj["username"])) {
					$users = $auth->getDelegateUsers();
					$users[] = $auth->getUser();
					foreach($users as $user) {
						if ($user == $obj["username"]) {
							return $this->getByUser($user);
						}
					}
					$this->output->add("status", "User has no right to do this (not in delegated list).");
					$this->output->add("users", []);
					return [];
				} else {
					return $this->getDelegated($write);
				}
			} else {
				if (!isset($obj["username"]) || $obj["username"] == $auth->getUser()) {
					return $this->getByUser($auth->getUser());
				} else {
					$this->output->add("status","User has no right to do this (not the user).");
					$this->output->add("users", []);
					return [];
				}
			}

		}

		/**
		 * Return a user by his name
		 * @param $username name of the user
		 * @param $write (optional) if true user object is given to output module
		 * @return user object
		 */
		private function getByUser($username, $write = true) {

			$sql = "SELECT user_name, (user_authdata IS NOT NULL) AS user_login FROM users WHERE user_name = :user_name";
		
			$params = array(":user_name" => $username);

			$out = $this->db->query($sql, $params, DB::F_ARRAY);

			if (count($out) < 1) {
				if ($write) {
					$this->output->add("users", []);
				}
				return [];
			}

			$out["modules"] = $this->getActiveModules($out[0]);

			$right_sql = "SELECT api_right FROM api_access WHERE api_user = :api_user";
			$right_params = array(
				":api_user" => $username
			);

			$rights = $this->db->query($right_sql, $right_params, DB::F_ARRAY);
			$out[0]["user_rights"] = [];

			foreach($rights as $right) {
				$out[0]["user_rights"][] = $right["api_right"];
			}

			if ($write) {
				$this->output->add("users", $out);
			}

			return $out;
		}

		public function deleteRight($user) {


			if (!isset($user["user_name"]) || empty($user["user_name"])) {
				$this->output->add("status", "No user is set.");
				return false;
			}

			if (!isset($user["user_right"]) || empty($user["user_right"])) {
				$this->output->add("status", "No right is set.");
				return false;
			}

			$auth = Auth::getInstance($this->db, $this->output);

			if (!$auth->hasRight("admin")) {
				$this->output->add("status", "Not allowed.");
				return false;
			}

			$sql = "DELETE FROM api_access WHERE :api_user = api_user AND api_right = :api_right)";

			$params = array(
				":api_user" => $user["user_name"],
				":api_right" => $user["user_right"]
			);

			return $this->db->insert($sql, [$params]);
		}
		public function addRight($user) {


			if (!isset($user["user_name"]) || empty($user["user_name"])) {
				$this->output->add("status", "No user is set.");
				return false;
			}

			if (!isset($user["user_right"]) || empty($user["user_right"])) {
				$this->output->add("status", "No right is set.");
				return false;
			}

			$auth = Auth::getInstance($this->db, $this->output);

			if (!$auth->hasRight("admin")) {
				$this->output->add("status", "Not allowed.");
				return false;
			}

			$sql = "INSERT INTO api_access (api_user, api_right) VALUES (:api_user, :api_right)";

			$params = array(
				":api_user" => $user["user_name"],
				":api_right" => $user["user_right"]
			);

			return $this->db->insert($sql, [$params]);
		}

		public function updateRights($user) {
			if (!isset($user["user_name"]) || empty($user["user_name"])) {
				$this->output->add("status", "No user is set.");
				return false;
			}

			if (!isset($user["user_rights"]) || empty($user["user_rights"])) {
				$this->output->add("status", "No rights is set.");
				return false;
			}
			$auth = Auth::getInstance($this->db, $this->output);

			if (!$auth->hasRight("admin")) {
				$this->output->add("status", "Not allowed.");
				return false;
			}

			$sql = "DELETE FROM api_rights WHERE api_user = :api_user";

			$params = array(
				":api_user" => $user["user_name"]
			);

			$this->db->beginTransaction();

			if (!$this->db->insert($sql, [$params])) {
				$this->db->rollback();
				return;
			}

			foreach($user["user_rights"] as $right) {
				if (!$this->addRight(array(
					"user_right" => $right,
					"user_name" => $user["user_name"]
				))) {
					$this->db->rollback();
					return;
				}
			}

			$this->db->commit();
		}

		/**
		 * Return a list of active modules for the given user
		 * @param user object with
		 * 	user_name name of the user
		 */
		public function getActiveModules($user) {
			global $modulelist;
			$modules = array();

			foreach ($modulelist as $name => $address) {
				if ($name != "User") {
				
					$module = getModuleInstance($name, $this->db, $this->output);
			
					if (isset($module)) {
						$modules[$name] = $module->isActive($user["user_name"]);
					}
				} else {
					$modules[$name] = $user["user_login"] == 1;
				}
			}

			return $modules;
		}

		/**
		 * Return if the module is active for the user
		 * @param username
		 * @return true or false
		 */
		public function isActive($username) {

			if (!isset($username) || empty($username)) {
				return false;
			}

			$obj = array("username" => $username);

			return ($this->get($obj, false)["user_can_login"]);
		}

		/**
		 * Returns all users in database.
		 * @output_flags users the userlist
		 * @return list of users
		 */
		public function getAll() {

			$sql = "SELECT user_name, (user_authdata IS NOT NULL) AS user_login FROM users";

			$out = $this->db->query($sql, array(), DB::F_ARRAY);

			foreach($out as $key => $user) {
				$out[$key]["modules"] = $this->getActiveModules($user);
			}

			$this->output->add("users", $out);

			return $out;

		}

		/**
		 * create a password hash.
		 * @param $salt salt for the password. If null then a random one will taken.
		 * @param $password the password
		 * @return if salt is null then $salt:sha265($salt, $password), else sha265($salt, $password);
		 */
		public function create_password_hash($salt, $password) {

			if (is_null($salt)) {
				$salt = uniqid(mt_rand(), true);
				
				$hash = $salt . ":" . hash("sha256", $salt . $password);
				return $hash;
			} else {
				return hash("sha265", $salt . $password);
			}
		}

		private function addDelegate($obj, $delegated = false) {
			
			$auth = Auth::getInstance($this->db, $this->output);

			if (!$auth->hasRight("admin") && !$delegated) {
				return false;
			}

			$sql = "INSERT INTO api_user_delegates (api_user, api_delegate) VALUES (:api_user, :api_delegate)";

			$params = array(
				":api_user" => $auth->getUser(),
				":api_delegate" => $obj["api_delegate"]
			);

			return $this->db->insert($sql, [$params]);
		}

		/**
		 * Adds a user to database.
		 * @param $user the user object. Every user needs at least a name.
		 *              Valid fields are:
		 *              	* user_name
		 *              	* user_authdata
		 * @output_flags user contains true or false when it failed.
		 * @return @see @output_flags
		 */
		public function add($user) {

			if (!isset($user["user_name"]) || empty($user["user_name"])) {
				$this->output->add("status", "Username is not set.");
				return false;
			}

			$auth = Auth::getInstance($this->db, $this->output);
			if (!$auth->hasRight("admin") && !$auth->hasRight("delegate")) {
				$this->output->add("status", "Not allowed");
				return false;
			}

			if (isset($user["user_authdata"]) && !empty($user["user_authdata"]) && $user["user_authdata"] !== "") {

				$user["user_authdata"] = $this->create_password_hash(null, $user["user_authdata"]);	
				
			} else {
				$user["user_authdata"] = null;
			}


			$sql = "INSERT INTO users(user_name, user_authdata) VALUES (:user_name, :user_authdata)";

			$params = array(
				":user_name" => $user["user_name"],
				":user_authdata" => $user["user_authdata"],
			);

			$this->db->beginTransaction();
			
			$id = $this->db->insert($sql, array($params));


			if (isset($user["user_rights"]) && !empty($user["user_rights"])) {
				foreach($user["user_rights"] as $right) {
					$this->addRight(array(
						"user_right" => $right,
						"user_name" => $user["user_name"]
					));
				}
			}

			if (isset($id) && !empty($id)) {
				$this->output->add("user", true);

				if ($auth->hasRight("delegate")) {
					$status = $this->addDelegate(array("api_delegate" => $user["user_name"]), true);

					if ($status < 1) {
						$this->db->rollback();
						return false;
					}
				}

				$this->db->commit();
				return true;
			} else {
				$this->output->add("user", false);
				$this->rollback();
				return false;
			}

		}

		/**
		 * Sets the password for the given user
		 * @param $user user object with
		 * 	- user_name name of the user
		 * 	- user_authdata password of the user (if null login is not possible)
		 * @return true or false
		 */
		public function set_password($user) {

			$auth = Auth::getInstance($this->db, $this->output);
			
			$test = false;
			if (!isset($user["user_name"]) || empty($user["user_name"])) {
				$this->output->add("status", "Username is not set.");
				return false;
			}

			if ($auth->hasRight("admin")) {
				$test = true;
			} else if ($auth->hasRight("delegate") && $auth->hasDelegatedUser($user["user_name"])) {
				$test = true;
			} else if ($auth->getUser() === $user["user_name"]) {
				$test = true;	
			}

			if (!$test) {
				$this->output->add("status", "Not allowed.");
				return false;
			}

			if (is_null($user["user_authdata"]) || $user["user_authdata"] === "") {
				$auth = null;
			} else {

				$auth = $this->create_password_hash(null, $user["user_authdata"]);
			}

			$sql = "UPDATE users SET user_authdata = :user_authdata WHERE user_name = :user_name";

			$params = array(
				":user_name" => $user["user_name"],
				":user_authdata" => $auth
			);

			$status = $this->db->insert($sql, array($params));

			if (isset($status)) {
				$this->output->add("status", "ok");
				return true;
			} else {
				return false;
			}
		}

		/**
		 * Deletes a user.
		 * @param $username name of the user
		 * @output_flags delete is "ok" when everything is fine, else "not ok"
		 * @return false on error, else true
		 */
		public function delete($obj) {
			$auth = Auth::getInstance($this->db, $this->output);
			if (!isset($obj["user_name"]) || empty($obj["user_name"])) {
				$this->output->add("status", "No username set.");
				return false;
			}
			$test = false;
			if ($auth->hasRight("admin") || ($auth->hasRight("delegate") && $auth->hasDelegatedUser($obj["user_name"]))) {
				$test = true;
			}

			if (!$test && $auth->getUser() !== $obj["user_name"]) {
				$this->output->add("status", "Not allowed.");
				return false;
			}
			
			$sql = "DELETE FROM users WHERE user_name = :username";

			$params = array(":username" => $obj["user_name"]);

			$status = $this->db->insert($sql, array($params));

			$this->output->addDebugMessage("delete", $status);
			if (isset($status)) {
				$this->output->add("delete", "ok");
				return true;
			} else {
				return false;
			}
		}
	}

?>
