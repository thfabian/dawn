//===--------------------------------------------------------------------------------*- C++ -*-===//
//                          _
//                         | |
//                       __| | __ ___      ___ ___
//                      / _` |/ _` \ \ /\ / / '_  |
//                     | (_| | (_| |\ V  V /| | | |
//                      \__,_|\__,_| \_/\_/ |_| |_| - Compiler Toolchain
//
//
//  This file is distributed under the MIT License (MIT).
//  See LICENSE.txt for details.
//
//===------------------------------------------------------------------------------------------===//

#ifndef DAWN_OPTIMIZER_STENCILINSTANTIATION_H
#define DAWN_OPTIMIZER_STENCILINSTANTIATION_H

#include "dawn/Optimizer/Accesses.h"
#include "dawn/Optimizer/Stencil.h"
#include "dawn/Optimizer/StencilFunctionInstantiation.h"
#include "dawn/SIR/SIR.h"
#include "dawn/Support/NonCopyable.h"
#include "dawn/Support/StringRef.h"
#include "dawn/Support/UIDGenerator.h"
#include <memory>
#include <set>
#include <string>
#include <unordered_map>

namespace dawn {

class OptimizerContext;

/// @brief Specific instantiation of a stencil
/// @ingroup optimizer
class StencilInstantiation : NonCopyable {
  OptimizerContext* context_;
  const sir::Stencil* SIRStencil_;
  const SIR* SIR_;

  /// Unique identifier generator
  UIDGenerator UIDGen_;

  /// Map of AccessIDs and to the name of the variable/field. Note that only for fields of the "main
  /// stencil" we can get the AccessID by name. This is due the fact that fields of different
  /// stencil functions can share the same name.
  std::unordered_map<std::string, int> NameToAccessIDMap_;
  std::unordered_map<int, std::string> AccessIDToNameMap_;

  /// Surjection of AST Nodes, Expr (FieldAccessExpr or VarAccessExpr) or Stmt (VarDeclStmt), to
  /// their AccessID. The surjection implies that multiple AST Nodes can have the same AccessID,
  /// which is the intended behaviour as we want to get the same ID back when we access the same
  /// field for example
  std::unordered_map<std::shared_ptr<Expr>, int> ExprToAccessIDMap_;
  std::unordered_map<std::shared_ptr<Stmt>, int> StmtToAccessIDMap_;

  /// Injection of AccessIDs of literal constant to their respective name (usually the name is just
  /// the string representation of the value). Note that literals always have *strictly* negative
  /// AccessIDs, which makes them distinguishable from field or variable AccessIDs. Further keep in
  /// mind that each access to a literal creates a new AccessID!
  std::unordered_map<int, std::string> LiteralAccessIDToNameMap_;

  /// This is a set of AccessIDs which correspond to fields. This allows to fully identify if a
  /// AccessID is a field, variable or literal as literals have always strictly negative IDs and
  /// variables are neither field nor literals.
  std::set<int> FieldAccessIDSet_;

  /// Set containing the AccessIDs of fields which are represented by a temporary storages
  std::set<int> TemporaryFieldAccessIDSet_;

  /// Set containing the AccessIDs of fields which are manually allocated by the stencil and serve
  /// as temporaries spanning over multiple stencils
  std::set<int> AllocatedFieldAccessIDSet_;

  /// Set containing the AccessIDs of "global variable" accesses. Global variable accesses are
  /// represented by global_accessor or if we know the value at compile time we do a constant
  /// folding of the variable
  std::set<int> GlobalVariableAccessIDSet_;

  /// Map of AccessIDs to the the list of all AccessIDs of the multi-versioned field. Note
  /// that the index in the vector corresponds to the version number.
  std::unordered_map<int, std::shared_ptr<std::vector<int>>> FieldVersionsMap_;

  /// Map of AccessIDs to the the list of all AccessIDs of the multi-versioned variables. Note
  /// that the index in the vector corresponds to the version number.
  std::unordered_map<int, std::shared_ptr<std::vector<int>>> VariableVersionsMap_;

  /// Encoding of the hierarchy: Stencil -> MultiStage -> Stage
  std::vector<std::shared_ptr<Stencil>> stencils_;

  /// Stencil description statements. These are built from the StencilDescAst of the sir::Stencil
  std::vector<std::shared_ptr<Statement>> stencilDescStatements_;
  std::unordered_map<std::shared_ptr<StencilCallDeclStmt>, int> StencilCallToStencilIDMap_;

  /// StageID to name Map. Filled by the `PassSetStageName`.
  std::unordered_map<int, std::string> StageIDToNameMap_;

  /// Referenced stencil functions in this stencil (note that nested stencil functions are not
  /// stored here but rather in the respecticve `StencilFunctionInstantiation`)
  std::vector<std::unique_ptr<StencilFunctionInstantiation>> stencilFunctionInstantiations_;
  std::unordered_map<std::shared_ptr<StencilFunCallExpr>, StencilFunctionInstantiation*>
      ExprToStencilFunctionInstantiationMap_;

public:
  /// @brief Assemble StencilInstantiation for stencil
  StencilInstantiation(OptimizerContext* context, const sir::Stencil* SIRStencil, const SIR* SIR);

  /// @brief Insert a new AccessID - Name pair
  void setAccessIDNamePair(int AccessID, const std::string& name);

  /// @brief Insert a new AccessID - Name pair of a field
  void setAccessIDNamePairOfField(int AccessID, const std::string& name, bool isTemporary = false);

  /// @brief Insert a new AccessID - Name pair of a global variable (i.e scalar field access)
  void setAccessIDNamePairOfGlobalVariable(int AccessID, const std::string& name);

  /// @brief Remove the field, variable or literal given by `AccessID`
  void removeAccessID(int AccesssID);

  /// @brief Get the name of the StencilInstantiation (corresponds to the name of the SIRStencil)
  const std::string& getName() const;

  /// @brief Get the `name` associated with the `AccessID`
  const std::string& getNameFromAccessID(int AccessID) const;

  /// @brief Get the `name` associated with the `StageID`
  const std::string& getNameFromStageID(int StageID) const;

  /// @brief Get the orginal `name` and a list of source locations of the field (or variable)
  /// associated with the `AccessID` in the given statement.
  std::pair<std::string, std::vector<SourceLocation>>
  getOriginalNameAndLocationsFromAccessID(int AccessID, const std::shared_ptr<Stmt>& stmt) const;

  /// @brief Get the original name of the field (as registered in the AST)
  std::string getOriginalNameFromAccessID(int AccessID) const;

  /// @brief Get the `name` associated with the literal `AccessID`
  const std::string& getNameFromLiteralAccessID(int AccessID) const;

  /// @brief Check whether the `AccessID` corresponds to a field
  bool isField(int AccessID) const { return FieldAccessIDSet_.count(AccessID); }

  /// @brief Check whether the `AccessID` corresponds to a temporary field
  bool isTemporaryField(int AccessID) const {
    return isField(AccessID) && TemporaryFieldAccessIDSet_.count(AccessID);
  }

  /// @brief Check whether the `AccessID` corresponds to a manually allocated field
  bool isAllocatedField(int AccessID) const {
    return isField(AccessID) && AllocatedFieldAccessIDSet_.count(AccessID);
  }

  /// @brief Get the set of fields which need to be allocated
  const std::set<int>& getAllocatedFieldAccessIDs() const { return AllocatedFieldAccessIDSet_; }

  /// @brief Check if the stencil instantiation needs to allocate fields
  bool hasAllocatedFields() const { return !AllocatedFieldAccessIDSet_.empty(); }

  /// @brief Check whether the `AccessID` corresponds to an accesses of a global variable
  bool isGlobalVariable(int AccessID) const { return GlobalVariableAccessIDSet_.count(AccessID); }
  bool isGlobalVariable(const std::string& name) const;

  /// @brief Get the value of the global variable `name`
  const sir::Value& getGlobalVariableValue(const std::string& name) const;

  /// @brief Check whether the `AccessID` corresponds to a variable
  bool isVariable(int AccessID) const { return !isField(AccessID) && !isLiteral(AccessID); }

  /// @brief Check whether the `AccessID` corresponds to a literal constant
  bool isLiteral(int AccessID) const {
    return AccessID < 0 && LiteralAccessIDToNameMap_.count(AccessID);
  }

  /// @brief Check whether the `AccessID` corresponds to a multi-versioned field
  bool isMultiVersionedField(int AccessID) const {
    return isField(AccessID) && FieldVersionsMap_.count(AccessID);
  }

  /// @brief Check whether the `AccessID` corresponds to a multi-versioned variable
  bool isMultiVersionedVariable(int AccessID) const {
    return isVariable(AccessID) && VariableVersionsMap_.count(AccessID);
  }

  /// @brief Get a list of all field AccessIDs of this multi-versioned field
  ArrayRef<int> getFieldVersions(int AccessID) const;

  enum RenameDirection {
    RD_Above, ///< Rename all fields above the current statement
    RD_Below  ///< Rename all fields below the current statement
  };

  /// @brief Add a new version to the field/local variable given by `AccessID`
  ///
  /// This will create a **new** field and trigger a renaming of all the remaining occurences in the
  /// AccessID maps either above or below that statement, starting one statment before or after the
  /// current statement. Optionally, an `Expr` can be passed which will be renamed as well (usually
  /// the left- or right-hand side of an assignment).
  ///
  /// Consider the following example:
  ///
  /// @code
  ///   v = 2 * u
  ///   lap = u(i+1)
  ///   u = lap(i+1)
  /// @endcode
  ///
  /// We may want to rename `u` in the second statement (an all occurences of `u` above) to
  /// resolve the race-condition. We expect to end up with:
  ///
  /// @code
  ///   v = 2 * u_1
  ///   lap = u_1(i+1)
  ///   u = lap(i+1)
  /// @endcode
  ///
  /// where `u_1` is the newly created version of `u`.
  ///
  /// @param AccessID   AccessID of the field for which a new version will be created
  /// @param stencil    Current stencil
  /// @param stageIdx   **Linear** index of the stage in the stencil
  /// @param stmtIdx    Index of the statement inside the stage
  /// @param expr       Expression to be renamed (usually the left- or right-hand side of an
  ///                   assignment). Can be `NULL`.
  /// @returns AccessID of the new field
  int createVersionAndRename(int AccessID, Stencil* stencil, int stageIndex, int stmtIndex,
                             std::shared_ptr<Expr>& expr, RenameDirection dir);

  /// @brief Rename all occurences of field `oldAccessID` to `newAccessID`
  void renameAllOccurrences(Stencil* stencil, int oldAccessID, int newAccessID);

  /// @brief Promote the local variable, given by `AccessID`, to a temporary field
  ///
  /// This will take care of registering the new field (and removing the variable) as well as
  /// replacing the variable accesses with point-wise field accesses.
  void promoteLocalVariableToTemporaryField(Stencil* stencil, int AccessID,
                                            const Stencil::Lifetime& lifetime);

  /// @brief Promote the temporary field, given by `AccessID`, to a real storage which needs to be
  /// allocated by the stencil
  void promoteTemporaryFieldToAllocatedField(int AccessID);

  /// @brief Demote the temporary field, given by `AccessID`, to a local variable
  ///
  /// This will take care of registering the new variable (and removing the field) as well as
  /// replacing the field accesses with varible accesses.
  ///
  /// This implicitcly assumes the first access (i.e `lifetime.Begin`) to the field is an
  /// `ExprStmt` and the field is accessed as the LHS of an `AssignmentExpr`.
  void demoteTemporaryFieldToLocalVariable(Stencil* stencil, int AccessID,
                                           const Stencil::Lifetime& lifetime);

  /// @brief Get the `AccessID` associated with the `name`
  ///
  /// Note that this only works for field and variable names, the mapping of literals AccessIDs and
  /// their name is a not bijective!
  int getAccessIDFromName(const std::string& name) const;

  /// @brief Get the `AccessID` of the Expr (VarAccess or FieldAccess)
  int getAccessIDFromExpr(const std::shared_ptr<Expr>& expr) const;

  /// @brief Get the `AccessID` of the Stmt (VarDeclStmt)
  int getAccessIDFromStmt(const std::shared_ptr<Stmt>& stmt) const;

  /// @brief Get StencilFunctionInstantiation of the `StencilFunCallExpr`
  StencilFunctionInstantiation*
  getStencilFunctionInstantiation(const std::shared_ptr<StencilFunCallExpr>& expr);
  const StencilFunctionInstantiation*
  getStencilFunctionInstantiation(const std::shared_ptr<StencilFunCallExpr>& expr) const;

  /// @brief Get StencilFunctionInstantiation of the `StencilFunCallExpr`
  std::unordered_map<std::shared_ptr<StencilFunCallExpr>, StencilFunctionInstantiation*>&
  getExprToStencilFunctionInstantiationMap();
  const std::unordered_map<std::shared_ptr<StencilFunCallExpr>, StencilFunctionInstantiation*>&
  getExprToStencilFunctionInstantiationMap() const;

  /// @brief Remove the stencil function given by `expr`
  ///
  /// If `callerStencilFunctionInstantiation` is not NULL (i.e the stencil function is called within
  /// the scope of another stencil function), the stencil function will be removed
  /// from the `callerStencilFunctionInstantiation` instead of this `StencilInstantiation`.
  void removeStencilFunctionInstantiation(
      const std::shared_ptr<StencilFunCallExpr>& expr,
      StencilFunctionInstantiation* callerStencilFunctionInstantiation = nullptr);

  /// @brief Register a new stencil function
  ///
  /// If `curStencilFunctionInstantiation` is not NULL, the stencil function is treated as a nested
  /// stencil function.
  StencilFunctionInstantiation*
  makeStencilFunctionInstantiation(const std::shared_ptr<StencilFunCallExpr>& expr,
                                   sir::StencilFunction* SIRStencilFun,
                                   const std::shared_ptr<AST>& ast, const Interval& interval,
                                   StencilFunctionInstantiation* curStencilFunctionInstantiation);

  /// @brief Get the list of stencils
  std::vector<std::shared_ptr<Stencil>>& getStencils() { return stencils_; }
  const std::vector<std::shared_ptr<Stencil>>& getStencils() const { return stencils_; }

  /// @brief Get StencilID of the StencilCallDeclStmt
  std::unordered_map<std::shared_ptr<StencilCallDeclStmt>, int>& getStencilCallToStencilIDMap();
  const std::unordered_map<std::shared_ptr<StencilCallDeclStmt>, int>&
  getStencilCallToStencilIDMap() const;

  /// @brief Get the StencilID of the StencilCallDeclStmt `stmt`
  int getStencilIDFromStmt(const std::shared_ptr<StencilCallDeclStmt>& stmt) const;

  /// @brief Get the stencil description AST
  const std::vector<std::shared_ptr<Statement>>& getStencilDescStatements() const {
    return stencilDescStatements_;
  }

  /// @brief Get the list of stencil functions
  std::vector<std::unique_ptr<StencilFunctionInstantiation>>& getStencilFunctionInstantiations() {
    return stencilFunctionInstantiations_;
  }
  const std::vector<std::unique_ptr<StencilFunctionInstantiation>>&
  getStencilFunctionInstantiations() const {
    return stencilFunctionInstantiations_;
  }

  /// @brief Get map which associates Exprs with AccessIDs
  std::unordered_map<std::shared_ptr<Expr>, int>& getExprToAccessIDMap();
  const std::unordered_map<std::shared_ptr<Expr>, int>& getExprToAccessIDMap() const;

  /// @brief Get map which associates Stmts with AccessIDs
  std::unordered_map<std::shared_ptr<Stmt>, int>& getStmtToAccessIDMap();
  const std::unordered_map<std::shared_ptr<Stmt>, int>& getStmtToAccessIDMap() const;

  /// @brief Get the AccessID-to-Name map
  std::unordered_map<std::string, int>& getNameToAccessIDMap();
  const std::unordered_map<std::string, int>& getNameToAccessIDMap() const;

  /// @brief Get the Name-to-AccessID map
  std::unordered_map<int, std::string>& getAccessIDToNameMap();
  const std::unordered_map<int, std::string>& getAccessIDToNameMap() const;

  /// @brief Get the Literal-AccessID-to-Name map
  std::unordered_map<int, std::string>& getLiteralAccessIDToNameMap();
  const std::unordered_map<int, std::string>& getLiteralAccessIDToNameMap() const;

  /// @brief Get the StageID-to-Name map
  std::unordered_map<int, std::string>& getStageIDToNameMap();
  const std::unordered_map<int, std::string>& getStageIDToNameMap() const;

  /// @brief Get the field-AccessID set
  std::set<int>& getFieldAccessIDSet();
  const std::set<int>& getFieldAccessIDSet() const;

  /// @brief Get the field-AccessID set
  std::set<int>& getGlobalVariableAccessIDSet();
  const std::set<int>& getGlobalVariableAccessIDSet() const;

  /// @brief Get the SIR
  const SIR* getSIR() const { return SIR_; }

  /// @brief Get the SIRStencil this context was built from
  const sir::Stencil* getSIRStencil() const { return SIRStencil_; }

  /// @brief Get the optimizer context
  OptimizerContext* getOptimizerContext() { return context_; }

  /// @brief Get a unique (positive) identifier
  int nextUID() { return UIDGen_.get(); }

  /// @brief Dump the StencilInstantiation to stdout
  void dump() const;

  /// @brief Dump the StencilInstantiation to a JSON file
  void dumpAsJson(std::string filename, std::string passName = "") const;

  /// @brief Generate a unique name for a local variable
  static std::string makeLocalVariablename(const std::string& name, int AccessID);

  /// @brief Generate a unique name for a temporary field
  static std::string makeTemporaryFieldname(const std::string& name, int AccessID);

  /// @brief Extract the name of a local variable
  ///
  /// Reverse the effect of `makeLocalVariablename`.
  static std::string extractLocalVariablename(const std::string& name);

  /// @brief Extract the name of a local variable
  ///
  /// Reverse the effect of `makeTemporaryFieldname`.
  static std::string extractTemporaryFieldname(const std::string& name);

  /// @brief Name used for all `StencilCallDeclStmt` in the stencil description AST
  /// (`getStencilDescStatements`) to signal code-gen it should insert a call to the gridtools
  /// stencil here
  static std::string makeStencilCallCodeGenName(int StencilID);

  /// @brief Check if the given name of a `StencilCallDeclStmt` was generate by
  /// `makeStencilCallCodeGenName`
  static bool isStencilCallCodeGenName(const std::string& name);

private:
  /// @brief Report the accesses to the console (according to `-freport-accesses`)
  void reportAccesses() const;
};

} // namespace dawn

#endif
